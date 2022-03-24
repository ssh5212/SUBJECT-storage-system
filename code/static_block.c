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
	int mapping_table[FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE / SECTOR_IN_BLOCK];

	char buffer_data[SECTOR_IN_BLOCK][SECTOR_SIZE + 1];
	int buffer_spare_lbn[SECTOR_IN_BLOCK];
	int buffer_spare_vaild[SECTOR_IN_BLOCK];

	fpos_t last_use_addr; // [정적]지우기 연산, [동적]쓰기 연산을 위해서 사용

	int round_count;
} dram;

struct temp {
	char data[SECTOR_SIZE + 1];
	long long spare_lbn;
	int spare_vaild;
	int origin_spare_lbn;
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
	temp.spare_lbn = NULL;
	temp.spare_vaild = NULL;
}

void buffer_init() {
	for (int i = 0; i < SECTOR_IN_BLOCK; i++) {
		dram.buffer_data[i][0] = '\0';
		dram.buffer_spare_lbn[i] = NULL;
		dram.buffer_spare_vaild[i] = NULL;
	}

}

int error = 0;

void init(FILE* flash_memory) {
	//매핑 테이블 초기화
	for (int i = 0; i < FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE / SECTOR_IN_BLOCK; i++) {
		dram.mapping_table[i] = i; 
	}
	dram.last_use_addr = 0;

	// 시스템 초기화
	counter.command_file_sequence += 1;
	counter.read_count[counter.command_file_sequence] = 0;
	counter.write_count[counter.command_file_sequence] = 0;
	counter.erase_count[counter.command_file_sequence] = 0;
	counter.time_required[counter.command_file_sequence] = 0;


	// 플래시 메모리 초기화
	flash_memory = fopen("flash_memory.txt", "w+"); 

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


void read(FILE* flash_memory, int pbn, int offset) {
	temp_init();
	flash_memory = fopen("flash_memory.txt", "r+"); 

	if (flash_memory == NULL) {
		printf("Flash Memory Error in read()..!\n");
	} 
	else {
		fseek(flash_memory, pbn + SECTOR_SIZE, SEEK_SET);
		fscanf(flash_memory, "%lld %d", &temp.spare_lbn, &temp.spare_vaild);
		//printf("%lld %d\n", temp.spare_lbn, temp.spare_vaild);
		fseek(flash_memory, pbn + offset, SEEK_SET);
		fread(temp.data, SECTOR_SIZE, 1, flash_memory); 
		counter.read_count[counter.command_file_sequence] += 1;																// read_counter++ 유효한 데이터 다시 써줌 
		fclose(flash_memory);
	}
	return;
}

void erase(FILE* flash_memory, int lbn, int pbn, int offset) {
	buffer_init();
	if(dram.round_count < FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE / SECTOR_IN_BLOCK) { // 블록의 개수만큼 돌지 않았으면
		read(flash_memory, dram.last_use_addr, 0 * PAGE_SIZE);
		if (temp.spare_vaild == 0) { // 해당 블록이 무효하다면
			flash_memory = fopen("flash_memory.txt", "r+");
			for (int i = 0; i < SECTOR_IN_BLOCK; i++) { // 블록 내 섹터 개수만큼 반복
				fprintf(flash_memory, "-1");
				for (int j = 0; j < 510; j++) {
					fprintf(flash_memory, " ");
				}
				fputs("-1       -1     ", flash_memory);
			}
			fclose(flash_memory);
		}
		else { // 해당 블록이 유효하다면
			dram.round_count++;
			dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE * SECTOR_IN_BLOCK) % (FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE * PAGE_SIZE); // 다음 블록으로 이동
			erase(flash_memory, lbn, pbn, offset);
		}
	}

	else { // 모든 블록이 유효하다면
		for(int i = 0; i < SECTOR_IN_BLOCK; i++) { // 블록 내 섹터 수 만큼 반복
			read(flash_memory, pbn, i * PAGE_SIZE); // read()로 명시 된 블록 위치를 읽음

			if (i == (offset / PAGE_SIZE)) { // 데이터가 무효 || 현재 쓰려는 offset의 위치라면
				continue;
			}
			else { // 데이터가 유효하다면
				strcpy(dram.buffer_data[i], temp.data);
				dram.buffer_spare_lbn[i] = (int)temp.spare_lbn;
				dram.buffer_spare_vaild[i] = temp.spare_vaild;
				dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE); 
			}
		}
		flash_memory = fopen("flash_memory.txt", "r+");
		for(int j = 0; j < SECTOR_IN_BLOCK; j++) { // 블록 내 섹터 수 만큼 반복
			if (dram.buffer_spare_lbn[j] != NULL) { // 섹터가 유효한 데이터라면 다시 써줌
				//printf("%d\n", dram.buffer_spare_lbn[j]);
				fseek(flash_memory, pbn + (j * PAGE_SIZE), SEEK_SET);
				fprintf(flash_memory, "%-6d", dram.buffer_spare_lbn[j]);
				fseek(flash_memory, pbn + (j * PAGE_SIZE) + SECTOR_SIZE, SEEK_SET);
				fprintf(flash_memory, "%-6d", dram.buffer_spare_lbn[j]);
				fseek(flash_memory, pbn + (j * PAGE_SIZE) + SECTOR_SIZE + 8, SEEK_SET);
				fputs(" -1     ", flash_memory);
				if (atoi(dram.buffer_data[j]) == -1) {
					counter.write_count[counter.command_file_sequence]--;
				}
 				counter.write_count[counter.command_file_sequence]++; 													// write_counter++ 기존 블록의 유효한 데이터 다시 써줌 
			}
			else { // 섹터가 무효한 데이터라면 날림
				fseek(flash_memory, pbn + (j * PAGE_SIZE), SEEK_SET);
				fprintf(flash_memory, "-1");
				for (int j = 0; j < 510; j++) {
					fprintf(flash_memory, " ");
				}
				fseek(flash_memory, pbn + (j * PAGE_SIZE) + SECTOR_SIZE, SEEK_SET);
				fputs("-1       -1     ", flash_memory);
			}
			fseek(flash_memory, pbn + offset, SEEK_SET);
			fprintf(flash_memory, "%-6d", lbn);
			fseek(flash_memory, pbn + offset + SECTOR_SIZE, SEEK_SET);
			fprintf(flash_memory, "%-6d", lbn);
			fseek(flash_memory, pbn + offset + SECTOR_SIZE + 8, SEEK_SET);
			fputs(" -1     ", flash_memory);
			counter.write_count[counter.command_file_sequence]++;
		}
		fclose(flash_memory);
	}
}

void write(FILE* flash_memory, int lbn, int pbn, int offset) {
	if (dram.round_count > (FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE) / SECTOR_IN_BLOCK) { // 처음부터 끝까지 다 돌았으면 
		dram.round_count = 0;
		erase(flash_memory, lbn, pbn, offset); // 지우기 연산 실행
		counter.erase_count[counter.command_file_sequence] += 1;															// erase_counter++ 
		return;
	} 
	else {
		read(flash_memory, pbn, offset); // read()로 명시 된 섹터 위치를 읽음

		if ((int)temp.spare_lbn == -1 && temp.spare_vaild == -1 && atoi(temp.data) == -1) { // 현재 위치가 사용 가능한 빈 블록이라면
			for(int i = 0; i < SECTOR_IN_BLOCK; i++) { // 블록 내 섹터 수 만큼 반복
				read(flash_memory, pbn, (i * PAGE_SIZE)); // read()로 명시 된 블록 위치를 읽음

				strcpy(dram.buffer_data[i], temp.data);
				dram.buffer_spare_lbn[i] = (int)temp.spare_lbn;
				dram.buffer_spare_vaild[i] = temp.spare_vaild;
				// dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE);
				if (atoi(temp.data) == -1) {
					counter.write_count[counter.command_file_sequence]--; 
				}
			}

			flash_memory = fopen("flash_memory.txt", "r+");
			for(int j = 0; j < SECTOR_IN_BLOCK; j++) { // 블록 내 섹터 수 만큼 반복	
				fseek(flash_memory, pbn + (j * PAGE_SIZE), SEEK_SET);
				fprintf(flash_memory, "%-6d", dram.buffer_spare_lbn[j]);
				fseek(flash_memory, pbn + (j * PAGE_SIZE) + SECTOR_SIZE, SEEK_SET);
				fprintf(flash_memory, "%-6d", lbn);
				fseek(flash_memory, pbn + (j * PAGE_SIZE) + SECTOR_SIZE + 8, SEEK_SET);
				fputs(" -1     ", flash_memory);
				counter.write_count[counter.command_file_sequence]++; 													// write_counter++ 기존 블록의 유효한 데이터 다시 써줌 
				// dram.last_use_addr = (dram.last_use_addr - PAGE_SIZE);
			}

			fseek(flash_memory, pbn + SECTOR_SIZE, SEEK_SET);
			fprintf(flash_memory, "%-6d", lbn);
			fseek(flash_memory, pbn + SECTOR_SIZE + 8, SEEK_SET);
			fputs(" -1     ", flash_memory);
			counter.write_count[counter.command_file_sequence]++; 													// write_counter++ 기존 블록의 유효한 데이터 다시 써줌 
			fclose(flash_memory);
			
			if (dram.round_count != 0 && temp.origin_spare_lbn == lbn) { // overwrite이고 기존 위치에 데이터가 자신의 것이라면
				dram.mapping_table[lbn] = (pbn / SECTOR_IN_BLOCK) / PAGE_SIZE;
			}
			return;
		}

		if ((int)temp.spare_lbn == temp.origin_spare_lbn && temp.spare_vaild == -1 && atoi(temp.data) == -1) { // 자기 자신의 블록이고 offset이 비어 있다면
			flash_memory = fopen("flash_memory.txt", "r+");
			fseek(flash_memory, pbn + offset, SEEK_SET);
			fprintf(flash_memory, "%d", lbn); // 데이터 작성
			fseek(flash_memory, pbn + offset + SECTOR_SIZE, SEEK_SET);
			fputs("-1       -1     ", flash_memory);
			counter.write_count[counter.command_file_sequence]++;
			fclose(flash_memory);
			return;
		}

		dram.round_count++;
		pbn = (pbn + PAGE_SIZE * SECTOR_IN_BLOCK) % ((FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE) * PAGE_SIZE); // 다음 섹터의 위치로 이동
		write(flash_memory, lbn, pbn, offset);

	}
}

void main() {
	FILE* flash_memory = NULL;
	FILE* command_file = NULL;
	long flash_size;

	char file_name[30] = {0};
	int max_sequence = 5;
	char command_file_name[5][20] = { "Copy of KODAK-total", "kodak-pattern", "kodak-pattern-rule", "linux", "NIKON-SS32" };

	char command_type[2] = {0};
	int lsn = 0;
	int lbn = 0;
	int pbn = 0;
	int offset = 0;
	
	counter.command_file_sequence = -1;

	// @here to start
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
			//printf("command_type, lsn = %s\t%d\n", command_type, lsn);
			
			if (strchr(command_type, 'w')) {
				temp.origin_spare_lbn = 0;
				dram.round_count = 0;
				lbn = lsn / 32;
				offset = (lsn % 32) * PAGE_SIZE;
				pbn = dram.mapping_table[lbn] * SECTOR_IN_BLOCK * PAGE_SIZE;
				write(flash_memory, lbn, pbn, offset);
			}
		}
		fclose(command_file);
		clock_t end = clock(); // 끝난 시간 체크
		counter.time_required[counter.command_file_sequence] = (double)(end - start) / CLOCKS_PER_SEC;
	}

	// Sequence 00 =====================================================
	// init(flash_memory);
	// command_file = fopen("NIKON-SS32.txt", "r");
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
	// 		lbn = lsn / 32;
	// 		temp.origin_spare_lbn = lbn;
	// 		offset = (lsn % 32) * PAGE_SIZE;
	// 		pbn = dram.mapping_table[lbn] * SECTOR_IN_BLOCK * PAGE_SIZE;
	// 		write(flash_memory, lbn, pbn, offset);
	// 	}
	// }
	// fclose(command_file);

	printf("\n");
	printf("====================================================================================================================================\n");
	printf("Static Block\n\n");

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