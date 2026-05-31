#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdint>

#include <openenclave/host.h>
#include <openenclave/attestation/verifier.h>
#include <openenclave/attestation/sgx/evidence.h>
#include <openenclave/bits/evidence.h>

#include "protocol.h"
#include "crypto.h"

namespace fs = std::filesystem;

static int cmd_gen_keys(const std::string& dir) {
    fs::create_directories(dir);
    std::string priv = dir + "/enclave_key.pem";
    std::string pub = dir + "/enclave_public.pem";

    std::string cmd = "openssl genrsa -out " + priv + " -3 3072";
    int rc = system(cmd.c_str());
    if (rc != 0) { std::cerr << "Failed to generate private key" << std::endl; return 1; }

    cmd = "openssl rsa -in " + priv + " -pubout -out " + pub;
    rc = system(cmd.c_str());
    if (rc != 0) { std::cerr << "Failed to generate public key" << std::endl; return 1; }

    std::cout << "[cli] Keys generated:\n  " << priv << "\n  " << pub << std::endl;
    return 0;
}

static int cmd_sign_enclave(const std::string& enclave_path,
                            const std::string& config_path,
                            const std::string& key_path,
                            const std::string& out_path) {
    if (!fs::exists(enclave_path)) { std::cerr << "Enclave not found: " << enclave_path << std::endl; return 1; }
    if (!fs::exists(config_path)) { std::cerr << "Config not found: " << config_path << std::endl; return 1; }
    if (!fs::exists(key_path)) { std::cerr << "Key not found: " << key_path << std::endl; return 1; }

    std::string cmd = "oesign sign -e " + enclave_path
                    + " -c " + config_path
                    + " -k " + key_path
                    + " -o " + out_path;
    std::cout << "[cli] Running: " << cmd << std::endl;
    int rc = system(cmd.c_str());
    if (rc != 0) { std::cerr << "oesign failed" << std::endl; return 1; }
    std::cout << "[cli] Signed enclave: " << out_path << std::endl;
    return 0;
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open " + path);
    f.seekg(0, std::ios::end);
    size_t n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(n);
    f.read((char*)buf.data(), n);
    return buf;
}

static int cmd_verify_quote(const std::string& quote_path,
                            const std::string& mode,
                            const std::string& expected_mrenclave) {
    auto quote = read_file(quote_path);
    std::cout << "[cli] Read quote: " << quote.size() << " bytes" << std::endl;

    // 选择格式
    oe_uuid_t format_id;
    if (mode == "dcap") {
        format_id = (oe_uuid_t)OE_FORMAT_UUID_SGX_ECDSA_P256;
    } else if (mode == "epid") {
        format_id = (oe_uuid_t)OE_FORMAT_UUID_SGX_EPID_LINKABLE;
    } else {
        std::cerr << "Unknown mode: " << mode << " (use dcap or epid)" << std::endl;
        return 1;
    }

    // 注册验证器
    oe_result_t rc;
    if (mode == "dcap") {
        rc = oe_verifier_initialize();
    } else {
        // IAS 模式需要额外配置
        std::cerr << "[cli] EPID/IAS mode requires IAS API key configuration" << std::endl;
        rc = oe_verifier_initialize();
    }
    if (rc != OE_OK) {
        std::cerr << "oe_verifier_initialize failed: " << oe_result_str(rc) << std::endl;
        return 1;
    }

    oe_verify_args_t args{};
    args.evidence_format = &format_id;
    args.evidence = quote.data();
    args.evidence_size = quote.size();
    args.endorsements = nullptr;
    args.endorsements_size = 0;

    // 自定义 claims 校验（可选）
    oe_claim_t* claims = nullptr;
    size_t num_claims = 0;
    rc = oe_verify_evidence(&args, &claims, &num_claims);
    if (rc != OE_OK) {
        std::cerr << "oe_verify_evidence failed: " << oe_result_str(rc) << std::endl;
        oe_free_claims(claims, num_claims);
        oe_verifier_shutdown();
        return 1;
    }

    std::cout << "[cli] Quote verification PASSED!" << std::endl;

    // 输出关键 claim
    for (size_t i = 0; i < num_claims; ++i) {
        std::string name = claims[i].name;
        if (name == OE_CLAIM_SGX_MRENCLAVE || name == OE_CLAIM_SGX_MRSIGNER ||
            name == OE_CLAIM_SECURITY_VERSION || name == OE_CLAIM_ISV_PROD_ID ||
            name == OE_CLAIM_REPORT_DATA) {
            std::cout << "  " << name << ": ";
            for (size_t j = 0; j < claims[i].value_size; ++j) {
                printf("%02x", claims[i].value[j]);
            }
            std::cout << std::endl;
        }
    }

    oe_free_claims(claims, num_claims);
    oe_verifier_shutdown();
    return 0;
}

static void print_help() {
    std::cout << "SGX Aggregator CLI\n\n"
              << "Usage: aggregator_cli <command> [options]\n\n"
              << "Commands:\n"
              << "  gen-keys --out <dir>\n"
              << "      Generate enclave signing key pair\n\n"
              << "  sign --enclave <path.so> --config <path.conf> --key <path.pem> --out <path.signed.so>\n"
              << "      Sign the enclave with oesign\n\n"
              << "  verify-quote --quote <path.bin> --mode <dcap|epid> [--mrenclave <hex>]\n"
              << "      Verify an SGX quote using DCAP or IAS\n\n"
              << "  help\n"
              << "      Show this help\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 1; }
    std::string cmd = argv[1];

    if (cmd == "help") { print_help(); return 0; }
    if (cmd == "gen-keys") {
        std::string out = "./signing";
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--out" && i + 1 < argc) out = argv[++i];
        }
        return cmd_gen_keys(out);
    }
    if (cmd == "sign") {
        std::string enc, cfg, key, out;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--enclave" && i + 1 < argc) enc = argv[++i];
            else if (a == "--config" && i + 1 < argc) cfg = argv[++i];
            else if (a == "--key" && i + 1 < argc) key = argv[++i];
            else if (a == "--out" && i + 1 < argc) out = argv[++i];
        }
        if (enc.empty() || cfg.empty() || key.empty()) {
            std::cerr << "Usage: sign --enclave <path.so> --config <path.conf> --key <path.pem> --out <path.signed.so>" << std::endl;
            return 1;
        }
        if (out.empty()) out = enc.substr(0, enc.find_last_of('.')) + ".signed.so";
        return cmd_sign_enclave(enc, cfg, key, out);
    }
    if (cmd == "verify-quote") {
        std::string quote, mode = "dcap", mrenclave;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--quote" && i + 1 < argc) quote = argv[++i];
            else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
            else if (a == "--mrenclave" && i + 1 < argc) mrenclave = argv[++i];
        }
        if (quote.empty()) {
            std::cerr << "Usage: verify-quote --quote <path.bin> --mode <dcap|epid>" << std::endl;
            return 1;
        }
        return cmd_verify_quote(quote, mode, mrenclave);
    }

    std::cerr << "Unknown command: " << cmd << std::endl;
    print_help();
    return 1;
}
