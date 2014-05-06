// Copyright 2014 <Artem Korneev>

#include <unistd.h>
#include <dirent.h>
#include <StormLib.h>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <functional>
#include <cerrno>

struct FileEntry {
    std::string  filename;
    std::string  fullPath;
    DWORD   fileSize;
    DWORD   compressedSize;
    // DWORD   lowDateTime;
    // DWORD   highDateTime;
    // LCID    locale;
    bool    isDir;
};

enum MODE {
    EXTRACT,
    LIST,
    ADD,
    DELETE,
};

std::string filename;
HANDLE archive;
std::vector<FileEntry> files;
std::vector<FileEntry> directories;

DWORD maxCompressedSize = 0;
DWORD maxFileSize = 0;

void printError(std::string message)
{
    int nError = GetLastError();

    message = "mpq: " + message;

    switch (nError) {
    case ERROR_BAD_FORMAT:
        std::cerr << message << ": Bad format" << "\n";
        break;
    case ERROR_NO_MORE_FILES:
        std::cerr << message << ": No more files" << "\n";
        break;
    case ERROR_FILE_CORRUPT:
        std::cerr << message << ": File corrupted" << "\n";
        break;
    default:
        perror(message.c_str());
        break;
    }
}

int intLength(DWORD num)
{
    int i = 0;
    do {
        ++i;
        num /= 10;
    } while (num);
    return i;
}

bool listFiles(std::string pattern)
{
    SFILE_FIND_DATA findData;
    std::set<std::string, std::less<std::string> > directoryNames;
    HANDLE handle = SFileFindFirstFile(archive, pattern.c_str(), &findData, 0);

    if (handle) {
        do {
            FileEntry e;

            e.filename          = findData.szPlainName;
            e.fullPath          = findData.cFileName;
            e.compressedSize    = findData.dwCompSize;
            e.fileSize          = findData.dwFileSize;
            // e.highDateTime      = findData.dwFileTimeHi;
            // e.lowDateTime       = findData.dwFileTimeLo;
            // e.locale            = findData.lcLocale;
            e.isDir = false;

            // TODO(hyperion): check the platform.
            std::replace(e.fullPath.begin(), e.fullPath.end(), '\\', '/');

            if (e.compressedSize > maxCompressedSize) {
                maxCompressedSize = e.compressedSize;
            }
            if (e.fileSize > maxFileSize) {
                maxFileSize = e.fileSize;
            }

            files.push_back(e);

            // Adding directories
            size_t offset = e.fullPath.rfind('/');
            if (offset != std::string::npos) {
                std::string filePath = e.fullPath.substr(0, offset + 1);
                size_t start = filePath.find('/', 0);

                while (start != std::string::npos) {
                    directoryNames.insert(filePath.substr(0, start + 1));
                    start = filePath.find('/', start + 1);
                }
            }
        } while (SFileFindNextFile(handle, &findData));

        SFileFindClose(handle);
    } else {
        printError("error finding pattern '" + pattern + "'");
        return false;
    }

    maxCompressedSize = intLength(maxCompressedSize);
    maxFileSize = intLength(maxFileSize);

    std::set<std::string>::iterator iter;
    for (iter = directoryNames.begin(); iter != directoryNames.end(); ++iter) {
        std::string fullPath = *iter;
        std::string dirName = fullPath.substr(0, fullPath.length() - 1);
        size_t offset = dirName.rfind('/');

        FileEntry e;

        e.filename = (offset != std::string::npos) ? dirName.substr(offset + 1) : dirName;
        e.fullPath = fullPath;
        e.compressedSize = 0;
        e.fileSize = 0;
        e.isDir = true;

        directories.push_back(e);
    }

    return true;
}

void printEntry(std::vector<FileEntry>::iterator entry)
{
    std::cout.width(maxFileSize);
    std::cout << entry->fileSize << " ";
    std::cout.width(maxCompressedSize);
    std::cout << entry->compressedSize << " ";
    std::cout << entry->fullPath << "\n";
}

void printList()
{
    std::vector<FileEntry>::iterator iter;
    for (iter = files.begin(); iter != files.end(); ++iter) {
        printEntry(iter);
    }

    for (iter = directories.begin(); iter != directories.end(); ++iter) {
        printEntry(iter);
    }
}

bool extractFile(std::vector<FileEntry>::iterator entry, std::string destinationFile)
{
    destinationFile += entry->fullPath;

    size_t offset = destinationFile.rfind('/');
    if (offset != std::string::npos) {
        std::string dest = destinationFile.substr(0, offset + 1);

        size_t start = dest.find('/', 0);
        while (start != std::string::npos) {
            std::string dirname = dest.substr(0, start);

            DIR* d = opendir(dirname.c_str());
            if (!d)
                mkdir(dirname.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
            else
                closedir(d);

            start = dest.find('/', start + 1);
        }
    }

    return SFileExtractFile(archive, entry->fullPath.c_str(), destinationFile.c_str(), 0);
}

void extractFiles(std::string destinationDirectory)
{
    if (!files.empty()) {
        std::cout << "\n";
        std::cout << "Extracting files..." << "\n";
        std::cout << "\n";

        if (destinationDirectory.at(destinationDirectory.length() - 1) != '/')
            destinationDirectory += "/";

        std::vector<FileEntry>::iterator iter;
        for (iter = files.begin(); iter != files.end(); ++iter) {
            if (extractFile(iter, destinationDirectory))
                std::cout << iter->fullPath << "\n";
            else
                printError("failed to extract the file '" + iter->fullPath + "'");
        }
    }
}


int main(int argc, char** argv)
{
    MODE mode = EXTRACT;
    std::string destinationDirectory = ".";

    int option;

    while ((option = getopt(argc, argv, "d:hZ")) != -1)
        switch (option) {
        case 'd':
            destinationDirectory = optarg;
            break;
        case 'Z':
            mode = LIST;
            break;
        case 'h':
        default:
            std::cout << "TODO: Add help page" << "\n";
            exit(EXIT_SUCCESS);
        }

    filename = argv[optind];

    if (!SFileOpenArchive(filename.c_str(), 0, MPQ_OPEN_READ_ONLY, &archive)) {
        printError("error opening an archive");
        exit(EXIT_FAILURE);
    }

    std::cout << "Archive: " << filename << "\n";

    std::string pattern = "*";
    bool listResult = false;

    switch (mode) {
    case LIST:
        if (argc > optind + 1)
            pattern = argv[optind + 1];
        if (listFiles(pattern))
            printList();
        break;
    case EXTRACT:
        if (argc > optind + 1)
            for (int i = optind + 1; i < argc; ++i)
                listResult = listFiles(std::string(argv[i]));
        else
            listResult = listFiles(pattern);

        if (listResult)
            extractFiles(destinationDirectory);
        break;
    default:
        break;
    }

    if (!SFileCloseArchive(archive)) {
        printError("error closing an archive");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
// kate: indent-mode cstyle; indent-width 4; replace-tabs on;
