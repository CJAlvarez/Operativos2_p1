#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define Where printf("Now at 0x%X\n\n", ftell(in));

typedef struct
{
    unsigned char first_byte;
    unsigned char start_chs[3];
    unsigned char partition_type;
    unsigned char end_chs[3];
    unsigned long start_sector;
    unsigned long length_sectors;
} __attribute((packed)) PartitionTable;

typedef struct
{
    unsigned char jmp[3];
    char oem[8];
    unsigned short sector_size;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char number_of_fats;
    unsigned short root_dir_entries;
    unsigned short total_sectors_short; // if zero, later field is used
    unsigned char media_descriptor;
    unsigned short fat_size_sectors;
    unsigned short sectors_per_track;
    unsigned short number_of_heads;
    unsigned long hidden_sectors;
    unsigned long total_sectors_long;
    unsigned char drive_number;
    unsigned char current_head;
    unsigned char boot_signature;
    unsigned long volume_id;
    char volume_label[11];
    char fs_type[8];
    char boot_code[448];
    unsigned short boot_sector_signature;
} __attribute((packed)) Fat16BootSector;

typedef struct
{
    unsigned char filename[8];
    unsigned char ext[3];
    unsigned char attributes;
    unsigned char reserved[10];
    unsigned short modify_time;
    unsigned short modify_date;
    unsigned short starting_cluster;
    unsigned long file_size;
} __attribute((packed)) Fat16Entry;

void print_file_info(Fat16Entry *entry)
{
    switch (entry->filename[0])
    {
    case 0x00:
        return; // unused entry
    case 0xE5:
        printf("Deleted file: [?%.7s.%.3s]\n", entry->filename + 1, entry->ext);
        return;
    case 0x05:
        printf("File starting with 0xE5: [%c%.7s.%.3s]\n", 0xE5, entry->filename + 1, entry->ext);
        break;
    case 0x2E:
        printf("Directory: [%.8s.%.3s]\n", entry->filename, entry->ext);
        break;
    default:
        printf("File: [%.8s.%.3s]\n", entry->filename, entry->ext);
    }

    printf("  Modified: %04d-%02d-%02d %02d:%02d.%02d    Start: [%04X]    Size: %d\n",
           1980 + (entry->modify_date >> 9), (entry->modify_date >> 5) & 0xF, entry->modify_date & 0x1F,
           (entry->modify_time >> 11), (entry->modify_time >> 5) & 0x3F, entry->modify_time & 0x1F,
           entry->starting_cluster, entry->file_size);
}

void ls_l(Fat16Entry *entry)
{
    int fileType = 0, i;
    // Caso de error
    switch (entry->filename[0])
    {
    case 0x00:
        return; // unused entry
    case 0xE5:
        return; // deleted file
    }

    if (entry->attributes == 0x10)
    {
        printf("d");
        fileType++;
    }
    else
    {
        printf("-");
    }
    for (i = 0; i < 3; i++)
    {
        switch (entry->attributes)
        {
        case 0x01: // Sólo lectura
            printf("r--");
            break;
        case 0x02: // Oculto
            printf("---");
            break;
        case 0x04: // Sistema, oculto
            printf("r--");
            break;
        case 0x08: // Volumen, sólo directorio raiz
            printf("r-x");
            break;
        case 0x10: // Subdirectorio
            printf("r-x");
            break;
        case 0x20: // Archivado
            printf("rwx");
            break;
        case 0x40: // No usado
            break;
        case 0x80: // No usado
            break;
        }
    }

    // Cantidad de enlaces internos
    printf(" 1 root root %d\t%02d %02d %02d:%02d ",
           entry->file_size,
           (entry->modify_date >> 5) & 0xF, entry->modify_date & 0x1F,
           (entry->modify_time >> 11), (entry->modify_time >> 5) & 0x3F);

    if (fileType == 0)
    {
        printf("%.8s.%.3s\n", entry->filename, entry->ext);
    }
    else
    {
        printf("%.8s\n", entry->filename);
    }
}

unsigned int int_to_int(unsigned int k)
{
    return (k == 0 || k == 1 ? k : ((k % 2) + 10 * int_to_int(k / 2)));
}

void cat_compuesto(FILE *in, Fat16BootSector *bs, char *name, char *content, char *ext)
{
    Fat16Entry entry;
    int i, lastDir = 0;

    // File Allocation Table
    char fat[bs->fat_size_sectors * bs->sector_size];
    fread(&fat, sizeof(fat), 1, in);

    // Encuentra la cantidad de dirEntries
    for (i = 0; i < bs->root_dir_entries; i++)
    {
        fread(&entry, sizeof(entry), 1, in);
        if (entry.file_size == 0 && entry.filename[0] == 0)
        {
            lastDir = i;
            break;
        }
    }
    fseek(in, ftell(in) - sizeof(entry), SEEK_SET); // Se mueve al ultimo dir entry

    // Obtener el siguente bloque disponible de la FAT
    for (lastDir = entry.starting_cluster * 2; lastDir < sizeof(fat) && fat[lastDir] != 0; lastDir += 2)
        ;

    // Crear dir entry
    char DE[sizeof(entry)];
    int j = 0;

    // FIlename
    for (i = 0; i < 8; ++i, j += 2)
        sprintf(DE + j, "%02x", name[i] & 0xff);

    // ext
    for (i = 0; i < 3; ++i, j += 2)
        sprintf(DE + j, "%02x", ext[i] & 0xff);

    // Attributes
    sprintf(DE + j, "%02x", ' ' & 0xff);
    j += 2;

    // Reserved
    for (i = 0; i < 10; ++i, j += 2)
        sprintf(DE + j, "%02x", '\0' & 0xff);

    // Time Created, uploaded
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    char tbin[16];
    // Hours to bin
    unsigned int hours = int_to_int(timeinfo->tm_hour);
    int nDigits = 0;
    if (hours > 0)
        nDigits = floor(log10(abs(hours))) + 1;

    for (i = 0; i < (5 - nDigits); i++)
        sprintf(tbin + i, "%d", 0);

    sprintf(tbin + i, "%d", hours);

    // Minutes to bin
    unsigned int minutes = int_to_int(timeinfo->tm_min);
    nDigits = 0;
    if (minutes > 0)
        nDigits = floor(log10(abs(minutes))) + 1;

    for (i = 0; i < (6 - nDigits); i++)
        sprintf(tbin + 5 + i, "%d", 0);

    sprintf(tbin + 5 + i, "%d", minutes);

    // Seconds to bin
    unsigned int seconds = int_to_int(timeinfo->tm_sec / 2);
    nDigits = 0;
    if (seconds > 0)
        nDigits = floor(log10(abs(seconds))) + 1;

    for (i = 0; i < (5 - nDigits); i++)
        sprintf(tbin + 11 + i, "%d", 0);

    sprintf(tbin + 11 + i, "%d", seconds);

    // Bin to Hex
    char bin[4];
    sprintf(bin, "%x", strtol(tbin, 0, 2) & 0xffff);
    for (int h = 0; h < 2; h++, j++)
    {
        sprintf(DE + j, "%c", bin[2 + h]);
    }
    for (int h = 0; h < 2; h++, j++)
    {
        sprintf(DE + j, "%c", bin[h]);
    }

    // Date Created, uploaded
    char dbin[16];

    // years to bin
    unsigned int years = int_to_int((timeinfo->tm_year - 80));
    nDigits = 0;
    if (years > 0)
        nDigits = floor(log10(abs(years))) + 1;

    for (i = 0; i < (7 - nDigits); i++)
        sprintf(dbin + i, "%d", 0);

    sprintf(dbin + i, "%d", years);

    // Months to bin
    unsigned int months = int_to_int(timeinfo->tm_mon + 1);
    nDigits = 0;
    if (months > 0)
        nDigits = floor(log10(abs(months))) + 1;

    for (i = 0; i < (4 - nDigits); i++)
        sprintf(dbin + 7 + i, "%d", 0);

    sprintf(dbin + 7 + i, "%d", months);

    // Days to bin
    unsigned int days = int_to_int(timeinfo->tm_mday);
    nDigits = 0;
    if (days > 0)
        nDigits = floor(log10(abs(days))) + 1;

    for (i = 0; i < (5 - nDigits); i++)
        sprintf(dbin + 11 + i, "%d", 0);

    sprintf(dbin + 11 + i, "%d", days);

    // Bin to Hex
    char bin2[4];
    sprintf(bin2, "%x", strtol(dbin, 0, 2) & 0xffff);

    for (int h = 0; h < 2; h++, j++)
    {
        sprintf(DE + j, "%c", bin2[2 + h]);
    }
    for (int h = 0; h < 2; h++, j++)
    {
        sprintf(DE + j, "%c", bin2[h]);
    }

    // Starting cluste number FAT
    nDigits = 0;
    if (lastDir > 0)
        nDigits = floor(log10(abs(lastDir))) + 1;

    if (nDigits == 2)
    {
        sprintf(DE + j, "%02x", lastDir & 0xff);
        j += 2;
        sprintf(DE + j, "%02x", '\0' & 0xff);
        j += 2;
    }
    else
    {
        char str[4];
        sprintf(str, "%d", lastDir);
        sprintf(DE + j, "%x", str[2] & 0xff);
        sprintf(DE + j, "%x", str[3] & 0xff);
        sprintf(DE + j, "%x", str[0] & 0xff);
        sprintf(DE + j, "%x", str[1] & 0xff);
        j += 4;
    }

    // File size in bytes
    nDigits = 0;
    if (strlen(content) > 0)
        nDigits = floor(log10(abs(strlen(content)))) + 1;

    if (nDigits == 2)
    {
        sprintf(DE + j, "%02x", strlen(content) & 0xff);
        j += 2;
        sprintf(DE + j, "%02x", '\0' & 0xff);
    }
    else if (nDigits < 4)
    {
        char str[4];
        sprintf(str, "%d", strlen(content));
        sprintf(DE + j, "%x", str[2] & 0xff);
        sprintf(DE + j, "%x", str[3] & 0xff);
        sprintf(DE + j, "%x", str[0] & 0xff);
        sprintf(DE + j, "%x", str[1] & 0xff);
    }
    else if (nDigits < 6)
    {
        char str[6];
        sprintf(str, "%d", strlen(content));
        sprintf(DE + j, "%x", str[2] & 0xff);
        sprintf(DE + j, "%x", str[3] & 0xff);
        sprintf(DE + j, "%x", str[0] & 0xff);
        sprintf(DE + j, "%x", str[1] & 0xff);
        sprintf(DE + j, "%x", str[4] & 0xff);
        sprintf(DE + j, "%x", str[5] & 0xff);
    }
    else
    {
        char str[8];
        sprintf(str, "%d", strlen(content));
        sprintf(DE + j, "%x", str[2] & 0xff);
        sprintf(DE + j, "%x", str[3] & 0xff);
        sprintf(DE + j, "%x", str[0] & 0xff);
        sprintf(DE + j, "%x", str[1] & 0xff);
        sprintf(DE + j, "%x", str[6] & 0xff);
        sprintf(DE + j, "%x", str[7] & 0xff);
        sprintf(DE + j, "%x", str[4] & 0xff);
        sprintf(DE + j, "%x", str[5] & 0xff);
    }

    // tryhard conversíon hex to ascii
    char output_dir[(strlen(DE) / 2) + 1];
    char caracter;
    char *dir;
    dir = (char *)malloc(strlen(&DE[0]));
    strcpy(dir, &DE[0]);

    for (int h = 0, k = 0; h < strlen(DE); h++, k += 2)
    {
        char *fact = (char *)malloc(2);
        sprintf(fact, "%c", dir[k]);
        sprintf(fact+1, "%c", dir[k+1]);

        sscanf(fact, "%x", &caracter);
        sprintf(output_dir + h, "%c", caracter);
    }
    fwrite(&output_dir, sizeof(output_dir), 1, in);

    // Coversi[on del contenido a Hex] 
    char hcontent[strlen(content) * 2];
    for (int h = 0, j = 0; h < strlen(content); h++, j += 2)
    {
        sprintf(hcontent + j, "%02x", content[h] & 0xff);
    }

    // Conversion de direccion inicial  Fat
    char start_fat[4];
    sprintf(start_fat, "%x", lastDir & 0xffff);

    // Conversion de tama;o contenido
    char size_content[8];
    sprintf(size_content, "%x", strlen(content) & 0xffffffff);

    
}

int main()
{
    FILE *in = fopen("test.img", "r+b");
    int i;
    PartitionTable pt[4]; // Tabla de particiones
    Fat16BootSector bs;   // Boot Sector (512 bytes)
    Fat16Entry entry;     //

    // ---------------------------------------------------------------------------->>>>>>> Partitión Table.

    fseek(in, 0x1BE, SEEK_SET);               // go to partition table start
    fread(pt, sizeof(PartitionTable), 4, in); // read all four entries

    /*
    Recorre cada partición (del contenido dentro de pt) 
    y al encontrar una partición válida(4, 6, 14) la elige y se detiene.
    */
    for (i = 0; i < 4; i++)
    {
        if (pt[i].partition_type == 4 || pt[i].partition_type == 6 ||
            pt[i].partition_type == 14)
        {
            //printf("FAT16 filesystem found from partition %d\n", i);
            break;
        }
    }

    if (i == 4)
    {
        //printf("No FAT16 filesystem found, exiting...\n");
        return -1;
    }

    // ---------------------------------------------------------------------------->>>>>>> Boot Sector.

    fseek(in, 512 * pt[i].start_sector, SEEK_SET);
    fread(&bs, sizeof(Fat16BootSector), 1, in);

    // ---------------------------------------------------------------------------->>>>>>> FAT.

    fseek(in, (bs.reserved_sectors - 1 + bs.fat_size_sectors) * bs.sector_size, SEEK_CUR); // Saltar la primera fat vacia
    int current = ftell(in);                                                               // Direccion guardada

    cat_compuesto(in, &bs, "Diego  ", "Esta es una prueba para el cat del archivo.", "cjt"); // Cat>[file].[ext] [content]

    fseek(in, current, SEEK_SET);                                // Se mueve de regreso a la direcci[on guardada
    fseek(in, (bs.fat_size_sectors) * bs.sector_size, SEEK_CUR); // Saltar la segunda Fat

    // ---------------------------------------------------------------------------->>>>>>> Root Directoy.

    printf("Now at 0x%X, sector size %d, FAT size %d sectors, %d FATs\n\n", ftell(in), bs.sector_size, bs.fat_size_sectors, bs.number_of_fats);

    for (i = 0; i < bs.root_dir_entries; i++)
    {
        fread(&entry, sizeof(entry), 1, in);
        ls_l(&entry);
    }

    //printf("\nRoot directory read, now at 0x%X\n", ftell(in));
    fclose(in);
    return 0;
}
//printf("Now at 0x%X\n\n", ftell(in));
// 2 3 4 5 6 7 8 9 10 11 12 13 f      *********** 2
// 00010600