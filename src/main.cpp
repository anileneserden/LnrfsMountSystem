#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <sys/stat.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <cstdlib>
#include "lnrfs.hpp"

// fuse_ops.cpp içerisindeki operasyon bağlayıcı fonksiyon
void init_fuse_operations(struct fuse_operations* ops);

void print_usage(const char* prog_name) {
    std::cout << "Kullanım:\n";
    std::cout << "  Mount etmek için:   " << prog_name << " -m <disk_imaji.img> <montaj_dizini> [-d]\n";
    std::cout << "  Umount etmek için:  " << prog_name << " -u <montaj_dizini>\n";
    std::cout << "  Formatlamak için:   " << prog_name << " <disk_imaji.img> --format [boyut_MB]\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // FORMAT İŞLEMİ: ./lfs <disk_imaji> --format [boyut_MB]
    if (argc >= 3 && std::string(argv[2]) == "--format") {
        std::string img_path = argv[1];
        std::cout << "[LNRFS] Disk formatlanıyor ve Superblock yazılıyor: " << img_path << std::endl;

        // 1. Hedef disk boyutunu belirle:
        // Argüman olarak MB verildiyse onu kullan, verilmediyse mevcut dosya boyutuna bak,
        // dosya yoksa veya 1 MiB'dan küçükse varsayılan 1024 MiB (1 GiB) yap.
        uint64_t target_bytes = 0;
        if (argc >= 4) {
            uint64_t mb = std::strtoull(argv[3], nullptr, 10);
            target_bytes = mb * 1024ULL * 1024ULL;
        } else {
            struct stat st;
            if (stat(img_path.c_str(), &st) == 0 && st.st_size > 1024 * 1024) {
                target_bytes = static_cast<uint64_t>(st.st_size);
            } else {
                target_bytes = 1024ULL * 1024ULL * 1024ULL; // Varsayılan 1 GiB
            }
        }

        uint32_t total_blocks = static_cast<uint32_t>(target_bytes / BLOCK_SIZE);
        if (total_blocks < 512) {
            std::cerr << "[LNRFS Hata] Disk boyutu çok küçük (en az 256 KB olmalı)." << std::endl;
            return 1;
        }

        // wb+ ile sıfırdan oluştur (sparse olarak büyütülecek)
        FILE* img_file = std::fopen(img_path.c_str(), "wb+");
        if (!img_file) {
            std::cerr << "[LNRFS Hata] İmaj dosyası açılamadı: " << img_path << std::endl;
            return 1;
        }

        g_img_file = img_file;

        // 2. Superblock alanını dinamik blok sayısına göre doldur
        g_lnr_sb.magic = LNRFS_MAGIC;
        g_lnr_sb.total_blocks = total_blocks;
        g_lnr_sb.inode_table_block = 1;
        g_lnr_sb.data_block_start = 256; // Kernel'da data_block_start dinamik okunuyorsa 2048 yapabilirsin

        size_t inode_table_bytes =
            (g_lnr_sb.data_block_start - g_lnr_sb.inode_table_block) * BLOCK_SIZE;
        g_lnr_inodes.clear();
        g_lnr_inodes.resize(inode_table_bytes / sizeof(LnrfsInode));

        // 3. İmajı toplam blok boyutuna getir (2 GiB+ için fseeko kullan)
        off_t last_byte_offset = (static_cast<off_t>(g_lnr_sb.total_blocks) * BLOCK_SIZE) - 1;
        fseeko(img_file, last_byte_offset, SEEK_SET);
        std::fputc(0, img_file);

        // 4. Dosyanın başına gidip superblock'u yaz
        fseeko(img_file, 0, SEEK_SET);
        std::fwrite(&g_lnr_sb, sizeof(LnrfsSuperblock), 1, img_file);

        // 5. Global inode tablosunu sıfırla
        for (size_t i = 0; i < g_lnr_inodes.size(); i++) {
            g_lnr_inodes[i].is_used = 0;
            g_lnr_inodes[i].is_directory = 0;
            g_lnr_inodes[i].size = 0;
            g_lnr_inodes[i].start_block = 0;
            g_lnr_inodes[i].filename[0] = '\0';
        }

        // 6. Inode tablosunu imaj dosyasına kaydet ve kapat
        lnrfs_save_image();
        std::fclose(img_file);
        g_img_file = nullptr;

        std::cout << "[LNRFS] Disk başarıyla formatlandı! Kapasite: "
                  << (target_bytes / (1024 * 1024)) << " MiB ("
                  << total_blocks << " blok)" << std::endl;
        return 0;
    }

    std::string mode = argv[1];

    // UNMOUNT İŞLEMİ (-u)
    if (mode == "-u") {
        std::string mount_point = argv[2];
        std::string cmd = "fusermount3 -u " + mount_point;
        std::cout << "Unmount ediliyor: " << mount_point << std::endl;
        int ret = std::system(cmd.c_str());
        return ret;
    }
    // MOUNT İŞLEMİ (-m)
    else if (mode == "-m") {
        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }
        std::string img_path = argv[2];
        std::string mount_point = argv[3];

        // -d (debug) bayrağı kontrolü
        bool debug_mode = false;
        for (int i = 4; i < argc; i++) {
            if (std::string(argv[i]) == "-d") {
                debug_mode = true;
                break;
            }
        }

        // Disk imajını yükle
        if (!lnrfs_load_image(img_path)) {
            std::cerr << "[LNRFS Hata] İmaj yüklenemedi veya sihirli bayt (magic number) uyuşmuyor!" << std::endl;
            return 1;
        }

        // FUSE için argüman listesi hazırla
        std::vector<char*> fuse_argv;
        fuse_argv.push_back(argv[0]);

        char flag_f[] = "-f"; // Ön planda tutar
        char flag_d[] = "-d"; // FUSE debug modu

        if (debug_mode) {
            fuse_argv.push_back(flag_f);
            fuse_argv.push_back(flag_d);
            std::cout << "LnrfsMountSystem DEBUG modunda ön planda başlatılıyor..." << std::endl;
        } else {
            std::cout << "LnrfsMountSystem arka planda başlatılıyor..." << std::endl;
        }

        fuse_argv.push_back(argv[3]); // mount_point

        struct fuse_operations ops;
        std::memset(&ops, 0, sizeof(ops));
        init_fuse_operations(&ops);

        int ret = fuse_main(fuse_argv.size(), fuse_argv.data(), &ops, NULL);

        // Çıkışta imajı güvenli kapat
        lnrfs_close_image();

        return ret;
    }
    else {
        print_usage(argv[0]);
        return 1;
    }
}