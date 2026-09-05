#ifndef LAMBO_FILE_H
#define LAMBO_FILE_H

#include <filesystem>
#include <string>

namespace lambo::file {

bool atomic_replace(const std::filesystem::path& source,
                    const std::filesystem::path& destination,
                    std::string& error);

} // namespace lambo::file

#endif // LAMBO_FILE_H
