/*
 * Bardzo prosty program zliczajacy niektore statystyki systemu plikow ext2
 * autorzy: Marek Jasinski, Adam Antoniuk
 * data: 21.05.2012
 */

#include "ext2_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define SUPER_BLOCK_OFF 1024

int fd; // deskryptor otwieranego pliku
struct ext2_super_block sb; // superblok
struct ext2_group_desc* gd_tab; // tablica deskryptorow grup
unsigned block_size; // rozmiar bloku obliczony na podstawie superbloku
unsigned blocks_per_group; // liczba blokow w grupie odczytana z superbloku
unsigned blocks_per_last_group = 0;	// obliczona liczba blokow w ostatniej grupie
unsigned inodes_per_group; // liczba inodow w grupie odczytana z superbloku
unsigned groups_count = 0; // obliczona liczba grup
unsigned all_blocks = 0; // obliczona liczba wszystkich blokow
unsigned all_free_blocks = 0; // obliczona liczba wszystkich wolnych blokow
unsigned all_inodes = 0; // obliczona liczba wszystkich inodow
unsigned all_free_inodes = 0; // obliczona liczba wszystkich wolnych inodow

// zlicza statystyki blokow w obrebie grupy na podstawie bitmapy blokow
void countBlocksStats(unsigned group_number);
// zlicza statystyki inodow w obrebie grupy na podstawie bitmapy inodow
void countInodesStats(unsigned group_number);

int main(int argc, char* argv[]) {
	if (argc != 2) {
		printf("usage: ext2_stats path_to_ext2_filesystem\n");
		return EXIT_FAILURE;
	}

	// otwarcie systemu plikow
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		fprintf(stderr, "Blad otwarcia pliku: %s\n", argv[1]);
		return EXIT_FAILURE;
	}

	// przesuniecie na poczatek superbloku
	if (lseek(fd, SUPER_BLOCK_OFF, SEEK_SET) != SUPER_BLOCK_OFF) {
		fprintf(stderr, "Blad ustawiania offsetu na poczatek superbloku\n");
		close(fd);
		return EXIT_FAILURE;
	}
	// odczytanie superbloku
	if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
		fprintf(stderr, "Blad odczytu superbloku\n");
		close(fd);
		return EXIT_FAILURE;
	}

	printf("############Z SUPERBLOKU########################\n");
	block_size = 1024 << sb.s_log_block_size;
	printf("Rozmiar bloku: %d\n", block_size);
	inodes_per_group = sb.s_inodes_per_group;
	printf("Liczba inodow w jednej grupie: %d\n", inodes_per_group);
	blocks_per_group = sb.s_blocks_per_group;
	printf("Liczba blokow w jednej grupie: %d\n", blocks_per_group);

	printf("############OBLICZONE###########################\n");
	unsigned tmp_blocks_count;
	tmp_blocks_count = sb.s_blocks_count;		// liczba wszystkich blokow
	tmp_blocks_count -= sb.s_first_data_block;	// odjecie liczby zarezerwowanych blokow
	// obliczenie liczby pelnych grup
	groups_count = tmp_blocks_count / blocks_per_group;
	// doliczenie ewentualnej niepelnej grupy
	// obliczenie liczby blokow w ostatniej grupie
	unsigned rest;
	rest = tmp_blocks_count % blocks_per_group;
	if (rest != 0) {
		blocks_per_last_group = rest;
		++groups_count;
	} else {
		blocks_per_last_group = blocks_per_group;
	}
	printf("Liczba grup: %d\n", groups_count);

	// utworzenie tablicy na deskryptory wszystkich grup
	gd_tab = (struct ext2_group_desc*) malloc(
			sizeof(struct ext2_group_desc) * groups_count);
	if (NULL == gd_tab) {
		fprintf(stderr, "Blad alokacji pamieci na deskryptory grup\n");
		close(fd);
		return EXIT_FAILURE;
	}
	// odczytanie deskryptorow wszystkich grup (bezposrednio za superblokiem)
	if (read(fd, gd_tab, sizeof(struct ext2_group_desc) * groups_count)
			!= sizeof(struct ext2_group_desc) * groups_count) {
		fprintf(stderr, "Blad odczytu deskryptorow grup\n");
		free(gd_tab);
		close(fd);
		return EXIT_FAILURE;
	}

	// przetwarzanie grup
	unsigned i = 0;
	for (; i < groups_count; ++i) {
		printf("Grupa: %d\n", i);
		countInodesStats(i);
		countBlocksStats(i);
	}

	// wypisanie obliczonych statystyk ogolnych
	printf("############OBLICZONE OGOLNE####################\n");
	printf("Liczba wszystkich inodow: %d\n", all_inodes);
	printf("Liczba wszystkich wolnych inodow: %d\n", all_free_inodes);
	printf("Liczba wszystkich blokow: %d\n", all_blocks);
	printf("Liczba wszystkich wolnych blokow: %d\n", all_free_blocks);

	// zwolnienie pamieci i zamkniecie deskryptora
	free(gd_tab);
	close(fd);
	return EXIT_SUCCESS;
}

void countBlocksStats(unsigned group_number) {
	unsigned offset = gd_tab[group_number].bg_block_bitmap * block_size;

	if (lseek(fd, offset, SEEK_SET) != offset) {
		fprintf(stderr, "Blad ustawiania offsetu na bitmape blokow\n");
		free(gd_tab);
		close(fd);
		exit(EXIT_FAILURE);
	}

	unsigned count, rest;
	if (group_number == groups_count - 1) { // ostatnia grupa
		count = blocks_per_last_group / 8;
		rest = blocks_per_last_group % 8;
	} else {
		count = blocks_per_group / 8;
		rest = blocks_per_group % 8;
	}

	/*
	 * if (rest != 0) // rest=1,2,3,4,5,6,7
	 * 	!!rest == 1
	 * else // rest=0
	 * 	!!rest == 0
	 */
	unsigned to_read = count + !!rest;
	char buffer[to_read];

	if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
		fprintf(stderr, "Blad odczytu bitmapy blokow\n");
		free(gd_tab);
		close(fd);
		exit(EXIT_FAILURE);
	}

	unsigned i, j;
	unsigned blocks = 0;
	unsigned free_blocks = 0;
	for (i = 0; i < count; ++i) {
		for (j = 0; j < 8; ++j) {
			++all_blocks;
			++blocks;
			if (!(buffer[i] & 0x01)) {
				++free_blocks;
				++all_free_blocks;
			}
			buffer[i] >>= 1;
		}
	}
	for (j = 0; j < rest; ++j) {
		++all_blocks;
		++blocks;
		if (!(buffer[i] & 0x01)) {
			++free_blocks;
			++all_free_blocks;
		}
		buffer[i] >>= 1;
	}
	// wypisanie statystyk
	printf("Liczba blokow w grupie: %d\n", blocks);
	printf("Liczba wolnych blokow w grupie: %d\n", free_blocks);
}

void countInodesStats(unsigned group_number) {
	unsigned offset = gd_tab[group_number].bg_inode_bitmap * block_size;

	if (lseek(fd, offset, SEEK_SET) != offset) {
		fprintf(stderr, "Blad ustawiania offsetu na bitmape inodow\n");
		free(gd_tab);
		close(fd);
		exit(EXIT_FAILURE);
	}

	unsigned count, rest;
	count = inodes_per_group / 8;
	rest = inodes_per_group % 8;

	unsigned to_read = count + !!rest;
	char buffer[to_read];

	if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
		fprintf(stderr, "Blad odczytu bitmapy inodow\n");
		free(gd_tab);
		close(fd);
		exit(EXIT_FAILURE);
	}

	unsigned i, j;
	unsigned inodes = 0;
	unsigned free_inodes = 0;
	for (i = 0; i < count; ++i) {
		for (j = 0; j < 8; ++j) {
			++all_inodes;
			++inodes;
			if (!(buffer[i] & 0x01)) {
				++free_inodes;
				++all_free_inodes;
			}
			buffer[i] >>= 1;
		}
	}
	for (j = 0; j < rest; ++j) {
		++all_inodes;
		++inodes;
		if (!(buffer[i] & 0x01)) {
			++free_inodes;
			++all_free_inodes;
		}
		buffer[i] >>= 1;
	}
	// wypisanie statystyk
	printf("Liczba inodow w grupie: %d\n", inodes);
	printf("Liczba wolnych inodow w grupie: %d\n", free_inodes);
}
