#pragma once

#include "vfs/platform.hpp"

// File interface
#include "vfs/directory_interface.hpp"
// Platform specific implementations
#if VFS_PLATFORM_WIN
#	include "vfs/win_directory.hpp"
#elif VFS_PLATFORM_POSIX
#   include "vfs/posix_directory.hpp"
#else
#	error No directory implementation defined for the current platform
#endif

#include "vfs/path.hpp"


namespace vfs {

    //----------------------------------------------------------------------------------------------
    using directory = directory_interface<directory_impl>;

    //----------------------------------------------------------------------------------------------
    inline bool create_path(const path &p)
    {
        auto folders = split_string(p.str(), path::separators());
        if (folders.empty())
        {
            // Invalid path, either empty or containing only path separators.
            return false;
        }

        size_t i = 0;
        std::string currentPath;
        const auto pathStr = p.str();
        // Test for remote location.
        // Those will look like \\foo\bar\ where foo is the remote computer.
        if ((pathStr.length() >= 2) && pathStr[0] == '\\' && pathStr[1] == '\\')
        {
            // The first folder will contain the name of the remote computer.
            currentPath = path::combine(path::separator(), path::separator(), folders[0], path::separator());
            ++i;
        }
        // Drive included in the path.
        // e.g. C:\foo\bar
        else if ((pathStr.length() >= 3) && pathStr[1] == ':' && pathStr[2] == '\\')
        {
            // The first folder will contain the name of the drive, e.g. 'C:'.
            currentPath = path::combine(folders[0], path::separator());
            ++i;
        }

        // Go through the folders list and create any of them that doesn't exit.
        for (; i < folders.size(); ++i)
        {
            currentPath += path::combine(folders[i], path::separator());
            if (!directory::exists(currentPath))
            {
                if (!directory::create_directory(currentPath))
                {
                    return false;
                }
            }
        }

        return true;
    }

    //----------------------------------------------------------------------------------------------
    inline bool delete_directory(const path &dirPath)
    {
        bool success = true;
        auto dir = directory(dirPath);
        dir.scan();
        for (const auto &subDir : dir.getSubDirectories())
        {
            success = success && delete_directory(subDir.getPath());
        }
        directory::delete_directory(dirPath);
//             for (const auto &f : dir.getFiles())
//             {
//                 file::delete_file(f);
//             }
        return success;
    }
    
    //----------------------------------------------------------------------------------------------
    inline bool move_directory(const path &src, const path &dst)
    {
        if (!directory::exists(src))
        {
            vfs_errorf("Source directory doesn't exists: %ws", src.c_str());
            return false;
        }

        if (directory::exists(dst))
        {
            vfs_errorf("Destination directory already exists: %ws", dst.c_str());
            return false;
        }

        if (!directory::create_directory(dst))
        {
            return false;
        }

        auto dir = directory(src);
        dir.scan();

        // Move all files in the new location.
        for (const auto &srcPath : dir.getFiles())
        {
            const auto &dstPath = path::combine(dst, extract_file_name(srcPath));
            file::move(srcPath, dstPath);
        }
        // Recursively create the sub directories hierarchy.
        for (auto &d : dir.getSubDirectories())
        {
            const auto &srcPath = d.getPath();
            const auto &dstPath = path::combine(dst, extract_file_name(srcPath));
            move_directory(srcPath, dstPath);
        }

        // Delete the source directory (should be empty by now).
        return delete_directory(src);
    }

} /*vfs*/
