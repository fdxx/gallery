#define STB_IMAGE_IMPLEMENTATION
#include <ImgSize.hpp>
#include <inja.hpp>
#include <JustifiedLayout.hpp>
#include <ArgsParser.hpp>

#include <print>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <dirent.h>

namespace fs = std::filesystem;
namespace jl = JustifiedLayout;
using json = nlohmann::json;

static constexpr double MAXWIDTH  = 1800.0;
static constexpr double MAXHEIGHT = 500.0;
static const char *ASSETSDIR = "assets";
static const std::unordered_set<std::string> IMAGE_EXTS = {
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff", ".tif"
};

class FileCounter
{
public:
    FileCounter(const fs::path &p)
    {
        m_Dir = p;
        m_lastCount = 0;
        std::ifstream file(SAVE_FILE);
        if (file)
            file >> m_lastCount;
    }

    bool IsChanged()
    {
        size_t curCount = CalcuFiles(m_Dir);
        if (curCount != m_lastCount)
        {
            m_lastCount = curCount;
            //持久化
            std::ofstream(SAVE_FILE, std::ios::trunc) << m_lastCount;
            return true;
        }
        return false;
    }

    size_t GetLastCount()
    {
        return m_lastCount;
    }

private:
    size_t CalcuFiles(const fs::path& p)
    {
        DIR *dir = ::opendir(p.c_str());
        if (!dir)
        {
            std::println(stderr, "Error: opendir failed: {}.", p.c_str());
            std::abort();
        }

        size_t count = 0;
        struct dirent *entry;
        while ((entry = ::readdir(dir)))
        {
            if (entry->d_name[0] == '.')
                continue;
            count++;
            if (entry->d_type == DT_DIR)
                count += CalcuFiles(p / entry->d_name);
        }
        ::closedir(dir);
        return count;
    }

    size_t m_lastCount;
    fs::path m_Dir;
    static inline const fs::path SAVE_FILE = ".filecount";
};


class GalleryGenerator
{
public:
    struct ImageInfo
    {
        ImageInfo(const fs::path &p, fs::file_time_type t, double r)
            : path(p), mtime(t), ratio(r) {}
            
        fs::path path; 
        fs::file_time_type mtime;
        double ratio;
    };

    struct DirNode
    {
        fs::path              absPath;
        std::vector<DirNode>  subdirs; 
        std::vector<ImageInfo> images;
        std::size_t totalImgs;
    };

    explicit GalleryGenerator(const fs::path &src, const jl::LayoutConfig &layout, const fs::path &out, const fs::path &assets)
    {
        m_srcRoot = fs::absolute(src);
        m_outRoot = fs::absolute(out);
        m_assets = assets;
        m_albumTemplate = m_assets / "templates" / "album.html";
        m_photoTemplate = m_assets / "templates" / "photo.html";
        m_sentinelFile = m_outRoot / ".gallery_sentine";
        m_srcLink = m_outRoot / "_src";

        m_LayoutCfg = layout;
        m_injaEnv.set_trim_blocks(true);
        m_injaEnv.set_lstrip_blocks(true);

        if (!fs::is_directory(m_srcRoot))
        {
            std::println(stderr, "Error: {} is not a directory.", m_srcRoot.c_str());
            std::abort();
        }

        if (!fs::exists(m_albumTemplate) || !fs::exists(m_photoTemplate))
        {
            std::println(stderr, "Error: HTML template does not exist.");
            std::abort();
        }
    }

    void GenSite()
    {
        ClearOutDir();

        fs::create_directories(m_outRoot);
        std::ofstream(m_sentinelFile).close();
        fs::create_symlink(m_srcRoot, m_srcLink);
        fs::copy(m_assets, m_outRoot, fs::copy_options::recursive);

        DirNode rootNode = BuildTree(m_srcRoot);
        GenDir(rootNode);
        std::println("[end] GenSite done.");
    }

    const fs::path &GetSrcDir() const
    {
        return m_srcRoot;
    } 

    const fs::path &GetOutDir() const
    {
        return m_outRoot;
    }

private:
    void ClearOutDir()
    {
        if (fs::exists(m_outRoot) && !fs::exists(m_sentinelFile))
        {
            std::println(stderr, "Error: Output directory lacks sentinel — refusing to delete.");
            std::abort();
        }
        fs::remove_all(m_outRoot);
    }

    DirNode BuildTree(const fs::path& dir)
    {
        DirNode node;
        node.absPath = dir;
        node.images = CollectImages(dir);
        node.totalImgs = node.images.size();

        for (const auto& e : fs::directory_iterator(dir))
        {
            if (!e.is_directory())
                continue;

            auto child = BuildTree(e.path());
            node.totalImgs += child.totalImgs;
            node.subdirs.push_back(std::move(child));
        }

        return node;
    }

    void GenDir(DirNode& node)
    {
        if (node.subdirs.empty())
        {
            GenPhotoPage(node);
            return;
        }

        GenAlbumPage(node);
        for (auto& sub : node.subdirs)
            GenDir(sub);
    }

    void GenAlbumPage(DirNode& node)
    {
        // 按照文件名称排序
        std::sort(node.subdirs.begin(), node.subdirs.end(), [](DirNode& a, DirNode& b) {
            return a.absPath < b.absPath;
        });

        json albums = json::array();
        for (const auto& sub : node.subdirs)
        {
            auto subRel = fs::relative(sub.absPath, m_srcRoot);
            auto &e = albums.emplace_back();
            e["name"]  = sub.absPath.filename().string();
            e["url"]   = "/" + (subRel / "index.html").generic_string();
            e["count"] = sub.totalImgs;
        }

        auto rel = fs::relative(node.absPath, m_srcRoot);
        json data;
        data["title"]   = (rel.empty() || rel == ".") ? std::string("Photo Gallery") : node.absPath.filename().string();
        data["albums"] = std::move(albums);
        data["navbar"]  = BuildNavbar(rel);

        fs::path out = m_outRoot / rel;
        fs::create_directories(out);
        std::ofstream ofs(out / "index.html");
        ofs << m_injaEnv.render_file(m_albumTemplate, data);
    }

    void GenPhotoPage(DirNode& node)
    {
        // 按照文件最后修改日期排序
        std::sort(node.images.begin(), node.images.end(), [](const ImageInfo& a, const ImageInfo& b){
            return a.mtime > b.mtime;
        });

        // Build justified layout
        std::vector<jl::InputItem> items;
        for (const auto& img : node.images)
            items.emplace_back(img.ratio);
        auto layout = jl::compute(items, m_LayoutCfg);

        json photos = json::array();
        for (std::size_t i = 0; i < node.images.size(); ++i)
        {
            const auto& box = layout.boxes[i];
            json &p = photos.emplace_back();
            p["src"]    = GetWebPath(node.images[i].path);
            p["name"]   = node.images[i].path.filename().string();
            p["top"]    = (int)std::round(box.top);
            p["left"]   = (int)std::round(box.left);
            p["width"]  = (int)std::round(box.width);
            p["height"] = (int)std::round(box.height);
        }

        auto rel = fs::relative(node.absPath, m_srcRoot);
        json data;
        data["title"]            = node.absPath.filename().string();
        data["count"]            = node.totalImgs;
        data["container_height"] = (int)std::ceil(layout.containerHeight);
        data["photos"]           = std::move(photos);
        data["navbar"]           = BuildNavbar(rel);

        auto out = m_outRoot / rel;
        fs::create_directories(out);
        std::ofstream ofs(out / "index.html");
        ofs << m_injaEnv.render_file(m_photoTemplate, data);
    }

    json BuildNavbar(const fs::path &rel) 
    {
        json navbar = json::array();
        fs::path acc;
        for (const auto &part : rel)
        {
            if (part == ".") continue;
            acc /= part;
            auto &crumb = navbar.emplace_back();
            crumb["name"] = part.string();
            crumb["url"]  = "/" + acc.generic_string() + "/index.html";
        }
        return navbar;
    }

    std::vector<ImageInfo> CollectImages(const fs::path& dir)
    {
        std::vector<ImageInfo> imgs;
        for (const auto& e : fs::directory_iterator(dir))
        {
            if (!e.is_regular_file())
                continue;
            if (!IsImage(e.path()))
                continue;
            imgs.emplace_back(e.path(), e.last_write_time(), GetImgRatio(e.path()));
        }
        
        return imgs;
    }

    std::string GetWebPath(const fs::path& absPath)
    {
        return "/_src/" + fs::relative(absPath, m_srcRoot).generic_string();
    }

    double GetImgRatio(const fs::path &path)
    {
        auto it = m_ImgRatioMap.find(path.generic_string());
        if (it != m_ImgRatioMap.end())
            return it->second;

        auto size = imgsize::from_file(path.c_str());
        if (!size)
        {
            std::println(stderr, "Failed to get image info: {}", path.c_str());
            return 0.0;
        }

        double ratio = double(size->width)/size->height;
        m_ImgRatioMap.emplace(path.generic_string(), ratio);
        return ratio;
    }

    bool IsImage(const fs::path& p)
    {
        return IMAGE_EXTS.contains(p.extension().string());
    }


    fs::path m_srcRoot;
    fs::path m_outRoot;
    fs::path m_assets;
    fs::path m_albumTemplate;
    fs::path m_photoTemplate;
    fs::path m_sentinelFile;
    fs::path m_srcLink;
    
    jl::LayoutConfig m_LayoutCfg;
    inja::Environment m_injaEnv;

    std::unordered_map<std::string, double> m_ImgRatioMap;
};


int main(int argc, char* argv[])
{
    ArgsParser args(argc, argv);
    if (!args.Has("-src") || !args.Has("-out"))
    {
        std::println("Usage: {} -src=dir -out=dir [-update=0] [-maxw=1800] [-maxh=500] [-assets=assets]", args.GetCmdName());
        return 1;
    }

    fs::path srcRoot = args.Get<std::string>("-src");
    fs::path outRoot = args.Get<std::string>("-out");
    fs::path assets = args.Get<std::string>("-assets", ASSETSDIR);
    int updateInterval = args.Get<int>("-update", 0);

    jl::LayoutConfig LayoutCfg;
    LayoutCfg.containerWidth            = args.Get<double>("-maxw", MAXWIDTH);
    LayoutCfg.targetRowHeight           = args.Get<double>("-maxh", MAXHEIGHT);
    LayoutCfg.targetRowHeightTolerance  = 0.25;
    LayoutCfg.containerPadding          = {0, 0, 0, 0};
    LayoutCfg.boxSpacing                = {6, 6};
    LayoutCfg.showWidows                = true;
    LayoutCfg.widowLayoutStyle          = jl::WidowLayoutStyle::Left;

    FileCounter fCounter(srcRoot);
    GalleryGenerator Gallery(srcRoot, LayoutCfg, outRoot, assets);

    std::println("[start] photos={} output={} updateInterval={}(seconds)", srcRoot.c_str(), outRoot.c_str(), updateInterval);
    
    if (updateInterval < 1)
    {
        Gallery.GenSite();
        return 0;
    }

    while (true)
    {
        size_t count = fCounter.GetLastCount();
        if (fCounter.IsChanged() || !fs::exists(Gallery.GetOutDir()))
        {
            std::println("[update] fileCount changed: {} -> {}", count, fCounter.GetLastCount());
            Gallery.GenSite();
        }

        std::this_thread::sleep_for(std::chrono::seconds(updateInterval));
    }

    return 0;
}


