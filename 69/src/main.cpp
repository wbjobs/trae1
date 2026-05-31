#include <fuse3/fuse.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "filesystem.h"
#include "fuse_ops.h"
#include "key_manager.h"
#include "user_manager.h"
#include "background_task.h"

static std::string g_password;

static std::string ReadPassword() {
    std::string password;
    termios oldt{};
    tcgetattr(STDIN_FILENO, &oldt);
    termios newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::cout << "Enter password: ";
    std::getline(std::cin, password);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << std::endl;
    return password;
}

static std::string ReadPasswordConfirm() {
    std::string p1 = ReadPassword();
    std::string p2 = ReadPassword();
    if (p1 != p2) {
        std::cerr << "Passwords do not match!" << std::endl;
        return "";
    }
    return p1;
}

static int DoInit(const std::string& root_dir) {
    if (!root_dir.empty() && root_dir.front() != '/') {
        std::cerr << "Error: root directory must be an absolute path" << std::endl;
        return 1;
    }

    std::string password = ReadPasswordConfirm();
    if (password.empty()) {
        return 1;
    }

    if (!KeyManager::Init(root_dir, password)) {
        std::cerr << "Error: Failed to initialize encrypted filesystem" << std::endl;
        KeyManager::SecureClear(password.data(), password.size());
        return 1;
    }

    if (!UserManager::Init(root_dir, password)) {
        std::cerr << "Error: Failed to initialize user management" << std::endl;
        KeyManager::SecureClear(password.data(), password.size());
        return 1;
    }

    std::cout << "Encrypted filesystem initialized successfully at: " << root_dir << std::endl;
    KeyManager::SecureClear(password.data(), password.size());
    return 0;
}

static std::unique_ptr<KeyManager> g_key_manager;
static std::unique_ptr<UserManager> g_user_manager;
static std::unique_ptr<FileSystem> g_filesystem;
static std::unique_ptr<BackgroundTask> g_bg_task;
static fusefs::FuseContext g_fuse_ctx{};

static void SignalHandler(int sig) {
    (void)sig;
    if (g_bg_task) g_bg_task->Stop();
    if (g_key_manager) g_key_manager->ClearKeys();
    if (g_user_manager) g_user_manager->ClearAll();
    KeyManager::SecureClear(g_password.data(), g_password.size());
    g_password.clear();
    _exit(0);
}

static int DoRepair(const std::string& root_dir) {
    if (!root_dir.empty() && root_dir.front() != '/') {
        std::cerr << "Error: root directory must be an absolute path" << std::endl;
        return 1;
    }

    g_password = ReadPassword();
    if (g_password.empty()) {
        return 1;
    }

    auto km = std::make_unique<KeyManager>();
    if (!km->Load(root_dir, g_password)) {
        std::cerr << "Error: Failed to load encrypted filesystem (wrong password?)" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    auto um = std::make_unique<UserManager>();
    if (!um->Load(root_dir)) {
        std::cerr << "Error: Failed to load user management" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    KeyManager::SecureClear(g_password.data(), g_password.size());
    g_password.clear();

    FileSystem fs(root_dir, *km, *um);

    size_t recovered = 0, failed = 0, total = 0;
    fs.ScanAndRepair(recovered, failed, total);

    km->ClearKeys();
    um->ClearAll();

    if (failed > 0) {
        return 1;
    }
    return 0;
}

static int DoAddUser(const std::string& root_dir, const std::string& new_username) {
    if (!root_dir.empty() && root_dir.front() != '/') {
        std::cerr << "Error: root directory must be an absolute path" << std::endl;
        return 1;
    }

    std::cout << "Authenticate as owner to add new user..." << std::endl;
    g_password = ReadPassword();
    if (g_password.empty()) return 1;

    auto km = std::make_unique<KeyManager>();
    if (!km->Load(root_dir, g_password)) {
        std::cerr << "Error: Failed to authenticate (wrong password?)" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    auto um = std::make_unique<UserManager>();
    if (!um->Load(root_dir)) {
        std::cerr << "Error: Failed to load user management" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    if (!um->Authenticate(OWNER_USER, g_password)) {
        std::cerr << "Error: Owner authentication failed" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    KeyManager::SecureClear(g_password.data(), g_password.size());
    g_password.clear();

    std::cout << "Enter password for new user '" << new_username << "':" << std::endl;
    std::string new_password = ReadPasswordConfirm();
    if (new_password.empty()) return 1;

    if (!um->AddUser(root_dir, new_username, new_password)) {
        std::cerr << "Error: Failed to add user" << std::endl;
        KeyManager::SecureClear(new_password.data(), new_password.size());
        return 1;
    }

    KeyManager::SecureClear(new_password.data(), new_password.size());

    if (!um->SaveRegistry(root_dir)) {
        std::cerr << "Error: Failed to save user registry" << std::endl;
        return 1;
    }

    std::cout << "User '" << new_username << "' added successfully." << std::endl;

    km->ClearKeys();
    um->ClearAll();
    return 0;
}

static int DoShare(const std::string& root_dir, const std::string& fuse_path,
                   const std::string& username) {
    if (!root_dir.empty() && root_dir.front() != '/') {
        std::cerr << "Error: root directory must be an absolute path" << std::endl;
        return 1;
    }

    std::cout << "Authenticate as owner..." << std::endl;
    g_password = ReadPassword();
    if (g_password.empty()) return 1;

    auto km = std::make_unique<KeyManager>();
    if (!km->Load(root_dir, g_password)) {
        std::cerr << "Error: Failed to authenticate (wrong password?)" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    auto um = std::make_unique<UserManager>();
    if (!um->Load(root_dir)) {
        std::cerr << "Error: Failed to load user management" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    if (!um->Authenticate(OWNER_USER, g_password)) {
        std::cerr << "Error: Owner authentication failed" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    KeyManager::SecureClear(g_password.data(), g_password.size());
    g_password.clear();

    if (!um->HasUser(username)) {
        std::cerr << "Error: User '" << username << "' does not exist" << std::endl;
        return 1;
    }

    FileSystem fs(root_dir, *km, *um);

    if (!fs.ShareFile(fuse_path, username)) {
        std::cerr << "Error: Failed to share file" << std::endl;
        return 1;
    }

    std::cout << "File '" << fuse_path << "' shared with user '" << username << "'." << std::endl;

    km->ClearKeys();
    um->ClearAll();
    return 0;
}

static int DoRevoke(const std::string& root_dir, const std::string& username) {
    if (!root_dir.empty() && root_dir.front() != '/') {
        std::cerr << "Error: root directory must be an absolute path" << std::endl;
        return 1;
    }

    if (username == OWNER_USER) {
        std::cerr << "Error: Cannot revoke owner" << std::endl;
        return 1;
    }

    std::cout << "Authenticate as owner..." << std::endl;
    g_password = ReadPassword();
    if (g_password.empty()) return 1;

    auto km = std::make_unique<KeyManager>();
    if (!km->Load(root_dir, g_password)) {
        std::cerr << "Error: Failed to authenticate (wrong password?)" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    auto um = std::make_unique<UserManager>();
    if (!um->Load(root_dir)) {
        std::cerr << "Error: Failed to load user management" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    if (!um->Authenticate(OWNER_USER, g_password)) {
        std::cerr << "Error: Owner authentication failed" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    KeyManager::SecureClear(g_password.data(), g_password.size());
    g_password.clear();

    if (!um->HasUser(username)) {
        std::cerr << "Error: User '" << username << "' does not exist" << std::endl;
        return 1;
    }

    FileSystem fs(root_dir, *km, *um);
    BackgroundTask bg(fs, *um);
    bg.Start();

    std::cout << "Revoking user '" << username << "' - re-encrypting shared files..." << std::endl;
    bg.RevokeUser(username);

    while (bg.GetPendingCount() > 0) {
        usleep(100000);
    }

    bg.Stop();

    um->RemoveUser(username);
    if (!um->SaveRegistry(root_dir)) {
        std::cerr << "Error: Failed to save user registry" << std::endl;
        return 1;
    }

    std::string user_key_path = root_dir + "/" + META_DIR + "/" + USERS_DIR + "/" + username;
    ::unlink(user_key_path.c_str());

    std::cout << "User '" << username << "' revoked. "
              << bg.GetCompletedCount() << " files re-encrypted." << std::endl;

    km->ClearKeys();
    um->ClearAll();
    return 0;
}

static int DoMount(const std::string& root_dir, const std::string& mount_point,
                   const std::string& username, int argc, char* argv[]) {
    if (!root_dir.empty() && root_dir.front() != '/') {
        std::cerr << "Error: root directory must be an absolute path" << std::endl;
        return 1;
    }
    if (!mount_point.empty() && mount_point.front() != '/') {
        std::cerr << "Error: mount point must be an absolute path" << std::endl;
        return 1;
    }

    g_password = ReadPassword();
    if (g_password.empty()) {
        return 1;
    }

    g_key_manager = std::make_unique<KeyManager>();
    if (!g_key_manager->Load(root_dir, g_password)) {
        std::cerr << "Error: Failed to load encrypted filesystem (wrong password?)" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    g_user_manager = std::make_unique<UserManager>();
    if (!g_user_manager->Load(root_dir)) {
        std::cerr << "Error: Failed to load user management" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    std::string mount_user = username.empty() ? OWNER_USER : username;
    if (!g_user_manager->Authenticate(mount_user, g_password)) {
        std::cerr << "Error: Authentication failed for user '" << mount_user << "'" << std::endl;
        KeyManager::SecureClear(g_password.data(), g_password.size());
        g_password.clear();
        return 1;
    }

    KeyManager::SecureClear(g_password.data(), g_password.size());

    g_filesystem = std::make_unique<FileSystem>(root_dir, *g_key_manager, *g_user_manager);
    g_bg_task = std::make_unique<BackgroundTask>(*g_filesystem, *g_user_manager);
    g_bg_task->Start();

    g_fuse_ctx.filesystem = g_filesystem.get();
    g_fuse_ctx.key_manager = g_key_manager.get();
    g_fuse_ctx.user_manager = g_user_manager.get();

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    struct fuse_operations ops{};
    std::memset(&ops, 0, sizeof(ops));
    ops.init = fusefs::FuseInit;
    ops.destroy = fusefs::FuseDestroy;
    ops.getattr = fusefs::FuseGetAttr;
    ops.readdir = fusefs::FuseReadDir;
    ops.open = fusefs::FuseOpen;
    ops.read = fusefs::FuseRead;
    ops.write = fusefs::FuseWrite;
    ops.create = fusefs::FuseCreate;
    ops.mkdir = fusefs::FuseMkDir;
    ops.unlink = fusefs::FuseUnlink;
    ops.rmdir = fusefs::FuseRmDir;
    ops.rename = fusefs::FuseRename;
    ops.truncate = fusefs::FuseTruncate;
    ops.flush = fusefs::FuseFlush;
    ops.release = fusefs::FuseRelease;
    ops.fsync = fusefs::FuseFsync;
    ops.statfs = fusefs::FuseStatFs;
    ops.access = fusefs::FuseAccess;

    std::vector<const char*> fuse_argv;
    fuse_argv.push_back("fusefs");
    fuse_argv.push_back("-f");
    fuse_argv.push_back("-o");
    fuse_argv.push_back("default_permissions");

    for (int i = 0; i < argc; ++i) {
        fuse_argv.push_back(argv[i]);
    }

    fuse_argv.push_back(mount_point.c_str());

    int fuse_argc = static_cast<int>(fuse_argv.size());
    std::vector<char*> fuse_argv_ptrs;
    for (const auto& arg : fuse_argv) {
        fuse_argv_ptrs.push_back(const_cast<char*>(arg));
    }

    std::cout << "Mounting encrypted filesystem at: " << mount_point << std::endl;
    std::cout << "User: " << mount_user << std::endl;
    std::cout << "Press Ctrl+C to unmount" << std::endl;

    int ret = fuse_main(fuse_argc, fuse_argv_ptrs.data(), &ops, &g_fuse_ctx);

    if (g_bg_task) g_bg_task->Stop();
    g_key_manager->ClearKeys();
    g_user_manager->ClearAll();
    g_filesystem.reset();
    g_key_manager.reset();
    g_user_manager.reset();
    g_bg_task.reset();

    return ret;
}

static void PrintUsage(const char* prog_name) {
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  " << prog_name << " --init <encrypted_directory>" << std::endl;
    std::cerr << "  " << prog_name << " --mount <encrypted_directory> <mount_point> [--user <name>] [fuse_options...]" << std::endl;
    std::cerr << "  " << prog_name << " --repair <encrypted_directory>" << std::endl;
    std::cerr << "  " << prog_name << " --adduser <encrypted_directory> <username>" << std::endl;
    std::cerr << "  " << prog_name << " --share <encrypted_directory> <fuse_path> <username>" << std::endl;
    std::cerr << "  " << prog_name << " --revoke <encrypted_directory> <username>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --init     Initialize a new encrypted filesystem" << std::endl;
    std::cerr << "  --mount    Mount an existing encrypted filesystem" << std::endl;
    std::cerr << "  --repair   Scan for temporary files and recover from crash" << std::endl;
    std::cerr << "  --adduser  Add a new user (owner only)" << std::endl;
    std::cerr << "  --share    Share a file with a user" << std::endl;
    std::cerr << "  --revoke   Revoke a user and re-encrypt their files" << std::endl;
    std::cerr << "  --user     Specify username for mount (default: owner)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Example:" << std::endl;
    std::cerr << "  " << prog_name << " --init /home/user/secure_data" << std::endl;
    std::cerr << "  " << prog_name << " --mount /home/user/secure_data /mnt/crypt" << std::endl;
    std::cerr << "  " << prog_name << " --mount /home/user/secure_data /mnt/crypt --user alice" << std::endl;
    std::cerr << "  " << prog_name << " --adduser /home/user/secure_data alice" << std::endl;
    std::cerr << "  " << prog_name << " --share /home/user/secure_data /documents/report.txt alice" << std::endl;
    std::cerr << "  " << prog_name << " --revoke /home/user/secure_data alice" << std::endl;
    std::cerr << "  " << prog_name << " --repair /home/user/secure_data" << std::endl;
}

int main(int argc, char* argv[]) {
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "--init") {
        if (argc < 3) {
            PrintUsage(argv[0]);
            return 1;
        }
        return DoInit(argv[2]);
    } else if (command == "--mount") {
        if (argc < 4) {
            PrintUsage(argv[0]);
            return 1;
        }
        std::string root_dir = argv[2];
        std::string mount_point = argv[3];
        std::string username;

        int extra_argc = 0;
        char** extra_argv = argv + 4;
        bool found_user = false;

        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--user" && i + 1 < argc) {
                username = argv[i + 1];
                found_user = true;
                i++;
            } else {
                extra_argv[extra_argc] = argv[i];
                extra_argc++;
            }
        }

        if (found_user) {
            return DoMount(root_dir, mount_point, username, extra_argc, extra_argv);
        }
        return DoMount(root_dir, mount_point, OWNER_USER, extra_argc, extra_argv);
    } else if (command == "--repair") {
        if (argc < 3) {
            PrintUsage(argv[0]);
            return 1;
        }
        return DoRepair(argv[2]);
    } else if (command == "--adduser") {
        if (argc < 4) {
            PrintUsage(argv[0]);
            return 1;
        }
        return DoAddUser(argv[2], argv[3]);
    } else if (command == "--share") {
        if (argc < 5) {
            PrintUsage(argv[0]);
            return 1;
        }
        return DoShare(argv[2], argv[3], argv[4]);
    } else if (command == "--revoke") {
        if (argc < 4) {
            PrintUsage(argv[0]);
            return 1;
        }
        return DoRevoke(argv[2], argv[3]);
    } else {
        PrintUsage(argv[0]);
        return 1;
    }
}
