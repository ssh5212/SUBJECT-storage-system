#define _CRT_SECURE_NO_WARNINGS // strtok 보안 경고로 인한 컴파일 에러 방지
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#define FLASH_MEMORY_SIZE 10 // MB 기준
#define SECTOR_IN_BLOCK 32
#define SECTOR_SIZE 512
#define SPARE_SIZE 16
#define PAGE_SIZE 528 // SECTOR_SIZE + SPARE_SIZE



struct dram {
	int mapping_table[FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE];

	char buffer_data[SECTOR_IN_BLOCK][SECTOR_SIZE + 1];
	int buffer_spare_lsn[SECTOR_IN_BLOCK];
	int buffer_spare_vaild[SECTOR_IN_BLOCK];

	int round_count;
	fpos_t last_use_addr; // 쓰기 연산 시 빈 위치를 찾아줌
	fpos_t last_vaild_addr; // 쓰기 연산이 가능한 가장 주소 값이 높은 위치
} dram;

struct temp {
	char data[SECTOR_SIZE + 1];
	long long spare_lsn;
	int spare_vaild;
	int orign_spare_lsn;
} temp;

struct counter {
	double read_count[5]; //읽기 count 값을 저장하는 변수
	double write_count[5]; //쓰기 count 값을 저장하는 변수
	double erase_count[5]; //지우기 count 값을 저장하는 변수
	double time_required[5]; // 소요 시간 체크
	
	int command_file_sequence; // 현재 읽고 있는 파일 순서
} counter;

void temp_init() {
	temp.data[0] = '\0';
	temp.spare_lsn = NULL;
	temp.spare_vaild = NULL;
}

void buffer_init() {
	for (int i = 0; i < SECTOR_IN_BLOCK; i++) {
		dram.buffer_data[i][0] = '\0';
		dram.buffer_spare_lsn[i] = NULL;
		dram.buffer_spare_vaild[i] = NULL;
	}

}

void init(FILE* flash_memory) {
	//매핑 테이블 초기화
	for (int i = 0; i < FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE; i++) {
		dram.mapping_table[i] = -1;
	}

	// dram 변수 초기화
	dram.last_use_addr = 0;
	dram.last_vaild_addr = FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE * (PAGE_SIZE); // 가장 마지막 주소

	// 시스템 초기화
	counter.command_file_sequence += 1;
	counter.read_count[counter.command_file_sequence] = 0;
	counter.write_count[counter.command_file_sequence] = 0;
	counter.erase_count[counter.command_file_sequence] = 0;
	counter.time_required[counter.command_file_sequence] = 0;

	// 플래시 메모리 초기화
	flash_memory = fopen("flash_memory.txt", "w");

	if (flash_memory == NULL) { // 파일 열기 에러 발생 시
		printf("Flash Memory Open Error..!");
	}
	else { // 제대로 동작하면
		for (int i = 0; i < FLASH_MEMORY_SIZE * 1024 * 1024 / 512; i++) { // 섹터 개수만큼 반복
			fprintf(flash_memory, "-1");
			for (int j = 0; j < 510; j++) {
				fprintf(flash_memory, " ");
			}
			fprintf(flash_memory, "-1       -1     ");
		}
	}
	fclose(flash_memory);

	printf("Mapping Table & Flash Memory & Count Initialization Complete..! [Sequence %d]\n", counter.command_file_sequence);
}


void read(FILE* flash_memory, unsigned int psn) {
	temp_init();
	flash_memory = fopen("flash_memory.txt", "r+");

	if (flash_memory == NULL) {
		printf("Flash Memory Error in read()..!\n");
	}
	else {
		fseek(flash_memory, psn, SEEK_SET);
		fread(temp.data, SECTOR_SIZE, 1, flash_memory);

		fscanf(flash_memory, "%lld %d", &temp.spare_lsn, &temp.spare_vaild);
		counter.read_count[counter.command_file_sequence] += 1;															// read_counter++ 
		fclose(flash_memory);
	}
	return;
}

void erase(FILE* flash_memory, int lsn) {
	buffer_init();
	dram.last_use_addr %= FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE * PAGE_SIZE; // dram.last_use_addr가 맨 끝을 가리키면 처음으로 바꿔줌

	if(dram.round_count < FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE / SECTOR_IN_BLOCK) { // 블록의 개수보다 작으면 실행
		int vaild_count = 0;
		for (int i = 0; i < SECTOR_IN_BLOCK; i++) { // 블록 내 섹터 수 만큼 반복
			read(flash_memory, dram.last_use_addr); // read()로 명시 된 섹터 위치를 읽음

			if (temp.spare_vaild == 0) { // 데이터가 무효하다면
				dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE);
				continue;
			}
			else { // 데이터가 유효하다면
				strcpy(dram.buffer_data[vaild_count], temp.data);
				dram.buffer_spare_lsn[vaild_count] = (int)&temp.spare_lsn;
				dram.buffer_spare_vaild[vaild_count] = &temp.spare_vaild;
				dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE);
			}
		}

		if (vaild_count >= SECTOR_IN_BLOCK) { // 블록 내 모든 섹터가 유효하다면
			dram.round_count++;
			// dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE);
			erase(flash_memory, lsn); // erase() 재귀
		}
		else { // 하나 이상 무효한 데이터가 있다면
			flash_memory = fopen("flash_memory.txt", "r+");
			dram.last_use_addr = dram.last_use_addr - (PAGE_SIZE * 31); // 블록의 가장 앞으로 이동
			for(int j = 0; j < vaild_count; j++) { // 유효한 데이터의 개수만큼 써줌
				fseek(flash_memory, dram.last_use_addr, SEEK_SET);
				fprintf(flash_memory, "%-6d", dram.buffer_spare_lsn[j]);
				fseek(flash_memory, dram.last_use_addr, SEEK_SET);
				fprintf(flash_memory, "%-6d", dram.buffer_spare_lsn[j]);
				fseek(flash_memory, dram.last_use_addr, SEEK_SET);
				fputs(" -1     ", flash_memory);
				counter.write_count[counter.command_file_sequence] += 1;														// write_counter++ 유효한 데이터 다시 써줌 
				dram.last_use_addr = dram.last_use_addr + PAGE_SIZE;
			}
			for(int k = vaild_count; k < SECTOR_IN_BLOCK; k++) { // 무효한 데이터의 개수만큼 비워줌
				fseek(flash_memory, dram.last_use_addr, SEEK_SET);
				fprintf(flash_memory, "-1");
				for (int j = 0; j < 510; j++) {
					fprintf(flash_memory, " ");
				}
				fseek(flash_memory, dram.last_use_addr, SEEK_SET);
				fputs("-1       -1     ", flash_memory);
				dram.last_use_addr = dram.last_use_addr + PAGE_SIZE;
			}
			counter.erase_count[counter.command_file_sequence] += 1;															// erase_counter++ 블록 지우기
			fclose(flash_memory);
			dram.last_vaild_addr = dram.last_use_addr;
			dram.last_use_addr = dram.last_use_addr - (PAGE_SIZE * 32);
		}
	}
	else { // 플래시 메모리 내 모든 섹터가 유효하다면
		flash_memory = fopen("flash_memory.txt", "r+");

		int origin_page_spare_vaild = dram.mapping_table[lsn] * (PAGE_SIZE)+SECTOR_SIZE + 9;
		fseek(flash_memory, origin_page_spare_vaild, SEEK_SET);
		fprintf(flash_memory, "%02d", 0); // 기존에 매핑 된 데이터 무효함을 표기

		counter.write_count[counter.command_file_sequence] += 1;																// write_counter++ 자신이 무효함 spare 표기
		fclose(flash_memory);
		erase(flash_memory, lsn);
	}
	return;
}

void write(FILE* flash_memory, int lsn) {
	if (dram.last_use_addr >= dram.last_vaild_addr) { // 사용 가능한 마지막 주소를 가리키면 
		erase(flash_memory, lsn); // 지우기 연산 실행
		write(flash_memory, lsn);
	}
	else {
		flash_memory = fopen("flash_memory.txt", "r+");
		if (flash_memory == NULL) {
			printf("Flash Memory Error in write()..!\n");
			return;
		}
		else {
			fseek(flash_memory, dram.last_use_addr, SEEK_SET);
			fprintf(flash_memory, "%-6d", lsn);
			fseek(flash_memory, dram.last_use_addr + SECTOR_SIZE, SEEK_SET);
			fprintf(flash_memory, "%-6d", lsn);
			fseek(flash_memory, dram.last_use_addr + SECTOR_SIZE + 8, SEEK_SET);
			fputs(" -1     ", flash_memory);
			counter.write_count[counter.command_file_sequence] += 1;															// write_counter++ 데이터 쓰기
		}
		
		if (dram.mapping_table[lsn] == -1) { // 기존에 매핑된 데이터가 없었다면
			dram.mapping_table[lsn] = dram.last_use_addr / PAGE_SIZE;
		}
		else { // 기존에 매핑된 데이터가 있었다면
			int origin_page_spare_vaild = dram.mapping_table[lsn] * (PAGE_SIZE)+SECTOR_SIZE + 9;
			fseek(flash_memory, origin_page_spare_vaild, SEEK_SET);
			fprintf(flash_memory, "%02d", 0); // 기존에 매핑 된 데이터 무효함을 표기
			counter.write_count[counter.command_file_sequence] += 1;															// write_counter++ 기존에 매핑된 데이터 무효 표기

			dram.mapping_table[lsn] = dram.last_use_addr / PAGE_SIZE;
		}
		fclose(flash_memory);
		dram.last_use_addr += PAGE_SIZE;
	}
}

void main() {
	FILE* flash_memory = NULL;
	FILE* command_file = NULL;
	long flash_size;

	char file_name[30] = { 0 };
	int max_sequence = 5;
	char command_file_name[5][20] = { "Copy of KODAK-total", "kodak-pattern", "kodak-pattern-rule", "linux", "NIKON-SS32" };

	char command_type[2] = { 0 };
	int lsn = 0;

	counter.command_file_sequence = -1;
	

	for (int f = 0; f < max_sequence; f++) {
		clock_t start = clock(); // 시작 시간 체크

		init(flash_memory);

		sprintf(file_name, "%s.txt", command_file_name[f]);
		command_file = fopen(file_name, "r");
		fseek(command_file, 0L, SEEK_END);
		flash_size = ftell(command_file);
		fseek(command_file, 0L, SEEK_SET);

		while (feof(command_file) == 0) { // 파일 끝까지 읽기
			if (ftell(command_file) == flash_size) {
				break;
			}

			fscanf(command_file, "%s\t%d", command_type, &lsn);

			if (strchr(command_type, 'w')) {
				temp.orign_spare_lsn = 0;
				dram.round_count = 0;
				write(flash_memory, lsn);
			}
		}
		fclose(command_file);
		clock_t end = clock(); // 끝난 시간 체크
		counter.time_required[counter.command_file_sequence] = (double)(end - start) / CLOCKS_PER_SEC;
	}
	// =============================== AUTO SELECT

	// Sequence 00 =====================================================
	// init(flash_memory);
	// command_file = fopen("b.txt", "r");
	// fseek(command_file, 0L, SEEK_END);
	// flash_size = ftell(command_file);
	// fseek(command_file, 0L, SEEK_SET);

	// while (feof(command_file) == 0) { // 파일 끝까지 읽기
	// 	if (ftell(command_file) == flash_size) {
	// 		break;
	// 	}

	// 	fscanf(command_file, "%s\t%d", command_type, &lsn);
	// 	printf("command_type, lsn = %s\t%d\n", command_type, lsn);

	// 	if (strchr(command_type, 'w')) {
	// 		dram.round_count = 0;
	// 		temp.orign_spare_lsn = 0;
	// 		write(flash_memory, lsn);
	// 	}
	// }
	// fclose(command_file);

	
	printf("\n");
	printf("====================================================================================================================================\n");
	printf("Dynamic Sector\n\n");
	
	for (int i = 0; i < 5; i++) {
		printf("== Sequence %02d =====================================================================================================================", i);
		printf("\n");

		printf("File_Name : %s\n", command_file_name[i]);
		printf("read_count[%d] : ", i);
		printf("%.0lf\t\t", counter.read_count[i]);
		printf("write_count[%d] : ", i);
		printf("%.0lf\t\t", counter.write_count[i]);
		printf("erase_count[%d] : ", i);
		printf("%.0lf\t\t", counter.erase_count[i]);
		printf("time_required[%d] : ", i);
		printf("%.1lf sec\n\n", counter.time_required[i]);
	}

	printf("====================================================================================================================================");

	return;
}