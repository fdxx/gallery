# Photo Gallery Generator

Scan an image folder to generate static HTML pages.

## Usage
```bash
Usage: ./gallery -src=dir -out=dir [-update=0] [-maxw=1800] [-maxh=500] [-assets=assets] [-webroot=/]
```

- `-src`: Source folder for images. Symbolic links will be created in the output folder.
- `-out`: Output folder for web files. You will need to use `nginx` or similar tools for display.
- `-update`: Interval for updating the site, in seconds. If the output folder is empty or the number of source files changes, the site will be automatically rebuilt. Default is 0 (disabled).
- `-maxw`: Maximum image width. Default is 1800
- `-maxh`: Maximum image height. Default is 500
- `-assets`: Website resource files, including CSS, website template files, etc. These will be copied to the website root directory when the site is generated. The default is `assets`, which is the runtime environment directory.
- `-webroot`: The website's root directory; the default is `/`. This is particularly useful when the website is located in a subdirectory.

## Requirements
- Linux
- C++23
- Clang

## Build

```bash
xmake f -c 
xmake -rv gallery
## Check the release folder.
```
