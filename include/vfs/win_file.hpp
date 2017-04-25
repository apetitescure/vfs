#pragma once

#include "vfs/platform.hpp"
#include "vfs/file_flags.hpp"
#include "vfs/path.hpp"


namespace vfs {

    //----------------------------------------------------------------------------------------------
    using file_impl = class win_file;
    //----------------------------------------------------------------------------------------------


    //----------------------------------------------------------------------------------------------
    class win_file
    {
    protected:
        using native_handle = HANDLE;

    protected:
        native_handle nativeHandle() const
        {
            return fileHandle_;
        }

        native_handle nativeFileMappingHandle() const
        {
            return fileMappingHandle_;
        }

        const path& fileName() const
        {
            return fileName_;
        }

        file_access fileAccess() const
        {
            return fileAccess_;
        }

    protected:
        win_file
        (
            const path              &name,
            file_access             access,
            file_creation_options   creationOption,
            file_flags              flags,
            file_attributes         attributes
        )
            : fileName_(name)
            , fileHandle_(INVALID_HANDLE_VALUE)
            , fileMappingHandle_(nullptr)
            , fileAccess_(access)
        {
            fileHandle_ = CreateFile
            (
                // File name
                fileName_.c_str(),
                // Access
                win_file_access(access),
                // TODO: Add shared access
                0,
                // Security attributes
                nullptr,
                // Creation options
                win_file_creation_options(creationOption),
                // Flags and attributes
                win_file_flags(flags) | win_file_attributes(attributes),
                // Template file
                nullptr
            );

            if (fileHandle_ == INVALID_HANDLE_VALUE)
            {
                const auto errorCode = GetLastError();
                vfs_errorf("CreateFile(%ws) failed with error: %s", fileName_.c_str(), get_last_error_as_string(errorCode).c_str());
            }
        }

        ~win_file()
        {
            close();
        }

    protected:
        bool isValid() const
        {
            return fileHandle_ != INVALID_HANDLE_VALUE;
        }

        bool isMapped() const
        {
            return fileMappingHandle_ != nullptr;
        }

        void close()
        {
            closeMapping();
            if (isValid())
            {
                CloseHandle(fileHandle_);
                fileHandle_ = INVALID_HANDLE_VALUE;
            }
        }

        int64_t size() const
        {
            vfs_check(isValid());

            int64_t fileSize = 0;
            if (!GetFileSizeEx(fileHandle_, PLARGE_INTEGER(&fileSize)))
            {
                const auto errorCode = GetLastError();
                vfs_errorf("SetEndOfFile(%ws) failed with error: %s", fileName_.c_str(), get_last_error_as_string(errorCode).c_str());
            }
            return fileSize;
        }

        bool resize()
        {
            vfs_check(isValid());

            if (!SetEndOfFile(fileHandle_))
            {
                const auto errorCode = GetLastError();
                vfs_errorf("SetEndOfFile(%ws) failed with error: %s", fileName_.c_str(), get_last_error_as_string(errorCode).c_str());
                return false;
            }
            return true;
        }

        bool skip(int64_t offset)
        {
            vfs_check(isValid());

            auto liDistanceToMove = LARGE_INTEGER{};
            liDistanceToMove.QuadPart = offset;
            if (!SetFilePointerEx(fileHandle_, liDistanceToMove, nullptr, FILE_CURRENT))
            {
                const auto errorCode = GetLastError();
                vfs_errorf("SetFilePointerEx(%ws) failed with error: %s", fileName_.c_str(), get_last_error_as_string(errorCode).c_str());
                return false;
            }
            return true;
        }

        int64_t read(uint8_t *dst, int64_t sizeInBytes)
        {
            vfs_check(isValid());

            auto numberOfBytesRead = DWORD{ 0 };
            if (!ReadFile(fileHandle_, (LPVOID)dst, DWORD(sizeInBytes), &numberOfBytesRead, nullptr))
            {
                const auto errorCode = GetLastError();
                vfs_errorf("ReadFile(%ws, %d) failed with error: %s", fileName_.c_str(), DWORD(sizeInBytes), get_last_error_as_string(errorCode).c_str());
            }
            return numberOfBytesRead;
        }

        int64_t write(const uint8_t *src, int64_t sizeInBytes)
        {
            vfs_check(isValid());

            auto numberOfBytesWritten = DWORD{ 0 };
            if (!WriteFile(fileHandle_, (LPCVOID)src, DWORD(sizeInBytes), &numberOfBytesWritten, nullptr))
            {
                const auto errorCode = GetLastError();
                vfs_errorf("WriteFile(%ws, %d) failed with error: %s", fileName_.c_str(), DWORD(sizeInBytes), get_last_error_as_string(errorCode).c_str());
            }
            return numberOfBytesWritten;
        }

        bool createMapping(int64_t viewSize)
        {
            const auto protect = DWORD((fileAccess_ == file_access::read_only) ? PAGE_READONLY : PAGE_READWRITE);
            fileMappingHandle_ = CreateFileMapping
            (
                // Handle to the file to map
                fileHandle_,
                // Security attributes
                nullptr,
                // Page protection level
                protect,
                // Mapping size, whole file if 0
                DWORD(viewSize >> 32), DWORD((viewSize << 32) >> 32),
                // File mapping object name for system wide objects, unsupported for now
                nullptr
            );

            if (fileMappingHandle_ == nullptr)
            {
                const auto errorCode = GetLastError();
                vfs_errorf("CreateFileMapping(%ws) failed with error: %s", fileName_.c_str(), get_last_error_as_string(errorCode).c_str());
                return false;
            }

            return true;
        }

        void closeMapping()
        {
            if (isMapped())
            {
                CloseHandle(fileMappingHandle_);
                fileMappingHandle_ = nullptr;
            }
        }

    private:
        path        fileName_;
        HANDLE      fileHandle_;
        HANDLE      fileMappingHandle_;
        file_access fileAccess_;

    private:
        static DWORD win_file_access(file_access access)
        {
            switch (access)
            {
            case file_access::read_only: return GENERIC_READ;
            case file_access::write_only: return GENERIC_WRITE;
            case file_access::read_write: return GENERIC_READ | GENERIC_WRITE;
            }

            vfs_check(false);
            return 0;
        }

        static DWORD win_file_creation_options(file_creation_options creationOption)
        {
            switch (creationOption)
            {
            case file_creation_options::create_if_nonexistant: return CREATE_NEW;
            case file_creation_options::create_or_overwrite: return CREATE_ALWAYS;
            case file_creation_options::open_if_existant: return OPEN_EXISTING;
            case file_creation_options::open_or_create: return OPEN_ALWAYS;
            case file_creation_options::truncate_existing: return TRUNCATE_EXISTING;
            }

            vfs_check(false);
            return 0;
        }

        static DWORD win_file_flags(file_flags flags)
        {
            auto f = DWORD{ 0 };

            if (uint32_t(flags) & uint32_t(file_flags::delete_on_close))
                f |= FILE_FLAG_DELETE_ON_CLOSE;

            if (uint32_t(flags) & uint32_t(file_flags::sequential_scan))
                f |= FILE_FLAG_SEQUENTIAL_SCAN;

            return f;
        }

        static DWORD win_file_attributes(file_attributes attributes)
        {
            auto attr = DWORD{ 0 };

            if (uint32_t(attributes) & uint32_t(file_attributes::normal))
                attr |= FILE_ATTRIBUTE_NORMAL;

            if (uint32_t(attributes) & uint32_t(file_attributes::temporary))
                attr |= FILE_ATTRIBUTE_TEMPORARY;

            return attr;
        }
    };
    //----------------------------------------------------------------------------------------------

} /*vfs*/
