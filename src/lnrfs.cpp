#include "lnrfs.hpp"
#include <iostream>
#include <cstring>

std::string g_img_path = "";
FILE* g_img_file = nullptr;
LnrfsSuperblock g_lnr_sb;
std::vector<LnrfsInode> g_lnr_inodes;

static bool is_valid_lnrfs_magic(uint32_t magic) {
    return magic == LNRFS_MAGIC_BE || magic == LNRFS_MAGIC_LE;
}

bool lnrfs_load_image(const std::string& img_path) {
    g_img_path = img_path;
    g_img_file = std::fopen(img_path.c_str(), "r+b");

    if (!g_img_file) {
        std::cerr << "Hata: Disk imaji acilamadi: " << img_path << std::endl;
        return false;
    }

    std::memset(&g_lnr_sb, 0, sizeof(LnrfsSuperblock));
    if (std::fread(&g_lnr_sb, sizeof(LnrfsSuperblock), 1, g_img_file) != 1) {
        std::cerr << "Hata: Superblock okunamadi!" << std::endl;
        std::fclose(g_img_file);
        g_img_file = nullptr;
        return false;
    }

    if (!is_valid_lnrfs_magic(g_lnr_sb.magic)) {
        std::cerr << "Hata: Gecersiz LNRFS imaji (Magic Number uyusmuyor)! Okunan: 0x"
                  << std::hex << g_lnr_sb.magic << std::dec << std::endl;
        std::fclose(g_img_file);
        g_img_file = nullptr;
        return false;
    }

    if (g_lnr_sb.inode_table_block >= g_lnr_sb.data_block_start ||
        g_lnr_sb.data_block_start >= g_lnr_sb.total_blocks) {
        std::cerr << "Hata: LNRFS bolum sinirlari gecersiz." << std::endl;
        std::fclose(g_img_file);
        g_img_file = nullptr;
        return false;
    }

    std::cout << "Superblock basariyla okundu." << std::endl;
    std::cout << "-> Toplam Blok Sayisi: " << g_lnr_sb.total_blocks << std::endl;
    std::cout << "-> Inode Tablo Bloku: " << g_lnr_sb.inode_table_block << std::endl;

    size_t inode_table_bytes =
        static_cast<size_t>(g_lnr_sb.data_block_start - g_lnr_sb.inode_table_block) * BLOCK_SIZE;
    size_t inode_count = inode_table_bytes / sizeof(LnrfsInode);
    if (inode_count == 0) {
        std::cerr << "Hata: LNRFS inode tablosu bos." << std::endl;
        std::fclose(g_img_file);
        g_img_file = nullptr;
        return false;
    }
    g_lnr_inodes.resize(inode_count);

    // Inode tablosunu çekirdeğin bildirdiği bloktan (inode_table_block * BLOCK_SIZE) oku
    long inode_offset = static_cast<long>(g_lnr_sb.inode_table_block) * static_cast<long>(BLOCK_SIZE);
    if (std::fseek(g_img_file, inode_offset, SEEK_SET) != 0 ||
        std::fread(g_lnr_inodes.data(), sizeof(LnrfsInode), inode_count, g_img_file) != inode_count) {
        std::cerr << "Hata: LNRFS inode tablosu okunamadi." << std::endl;
        std::fclose(g_img_file);
        g_img_file = nullptr;
        return false;
    }

    // --- DEBUG: Aktif inode'ları kontrol et ---
    int active_count = 0;
    for (size_t i = 0; i < g_lnr_inodes.size(); i++) {
        g_lnr_inodes[i].filename[LNRFS_MAX_FILENAME - 1] = '\0';
        if (g_lnr_inodes[i].is_used) {
            std::cout << "[LNRFS] Aktif Dosya/Klasor Bulundu -> Index: " << i
                      << ", Isim: " << g_lnr_inodes[i].filename
                      << ", Boyut: " << g_lnr_inodes[i].size << " bayt" << std::endl;
            active_count++;
        }
    }
    std::cout << "Toplam aktif dosya sayisi: " << active_count << std::endl;

    std::cout << "LNRFS imaji basariyla yuklendi ve inode'lar okundu." << std::endl;
    return true;
}

void lnrfs_save_image() {
    if (!g_img_file) return;

    // Superblock yaz (0. bloğun başına)
    std::rewind(g_img_file);
    std::fwrite(&g_lnr_sb, sizeof(LnrfsSuperblock), 1, g_img_file);

    // Inode tablosunu doğru bloğa yaz
    long inode_offset = static_cast<long>(g_lnr_sb.inode_table_block) * static_cast<long>(BLOCK_SIZE);
    std::fseek(g_img_file, inode_offset, SEEK_SET);
    std::fwrite(g_lnr_inodes.data(), sizeof(LnrfsInode), g_lnr_inodes.size(), g_img_file);

    std::fflush(g_img_file);
}

void lnrfs_close_image() {
    if (g_img_file) {
        lnrfs_save_image();
        std::fclose(g_img_file);
        g_img_file = nullptr;
    }
}