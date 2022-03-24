#define _CRT_SECURE_NO_WARNINGS // strtok 보안 경고로 인한 컴파일 에러 방지
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#define FLASH_MEMORY_SIZE 5 // MB 기준
#define SECTOR_IN_BLOCK 32
#define SECTOR_SIZE 512
#define SPARE_SIZE 16
#define PAGE_SIZE 528 // SECTOR_SIZE + SPARE_SIZE

struct dram {
    int mapping_table[FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE];

    char buffer_data[SECTOR_IN_BLOCK][SECTOR_SIZE + 1];
    int buffer_spare_lsn[SECTOR_IN_BLOCK];
    int buffer_spare_vaild[SECTOR_IN_BLOCK];

    int last_use_addr; // [정적]지우기 연산, [동적]쓰기 연산을 위해서 사용

    int round_count;
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


void read(FILE* flash_memory, int psn) {
    temp_init();
    flash_memory = fopen("flash_memory.txt", "r+");

    if (flash_memory == NULL) {
        printf("Flash Memory Error in read()..!\n");
    }
    else {
        fseek(flash_memory, psn, SEEK_SET);
        fread(temp.data, SECTOR_SIZE, 1, flash_memory);

        fscanf(flash_memory, "%lld %d", &temp.spare_lsn, &temp.spare_vaild);
        counter.read_count[counter.command_file_sequence] += 1;                                                // read_counter++ 유효한 데이터 다시 써줌 
        fclose(flash_memory);
    }
    return;
}

void erase(FILE* flash_memory, int lsn) {
    buffer_init();
    dram.last_use_addr = dram.last_use_addr % (FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE * PAGE_SIZE); // 다음 섹터의 위치로 이동

    if (dram.round_count < FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE / SECTOR_IN_BLOCK) { // 블록의 개수만큼 반복
        int vaild_count = 0;
        for (int i = 0; i < SECTOR_IN_BLOCK; i++) { // 블록 내 섹터 수 만큼 반복
            read(flash_memory, dram.last_use_addr); // read()로 명시 된 섹터 위치를 읽음

            if (temp.spare_vaild == 0) { // 데이터가 무효하다면
                dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE);
                continue;
            }
            else { // 데이터가 유효하다면
                strcpy(dram.buffer_data[i], temp.data);
                dram.buffer_spare_lsn[i] = (int)temp.spare_lsn;
                dram.buffer_spare_vaild[i] = temp.spare_vaild;
                dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE);

                vaild_count++;

            }
        }
        if (vaild_count >= SECTOR_IN_BLOCK) { // 블록 내 모든 섹터가 유효하다면
            dram.round_count++;
            // dram.last_use_addr = (dram.last_use_addr + PAGE_SIZE) % (FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE * PAGE_SIZE);
            erase(flash_memory, lsn); // erase() 재귀
        }
        else { // 하나 이상 무효한 데이터가 있다면
            for (int j = 0; j < SECTOR_IN_BLOCK; j++) { // 블록 내 섹터 수 만큼 반복
                if (dram.buffer_spare_lsn[j] != NULL) { // 섹터가 유효한 데이터라면 다시 써줌
                    flash_memory = fopen("flash_memory.txt", "r+");
                    fseek(flash_memory, dram.last_use_addr - (PAGE_SIZE * (32 - j)), SEEK_SET);
                    fprintf(flash_memory, "%d", dram.buffer_spare_lsn[j]);
                    fseek(flash_memory, dram.last_use_addr - (PAGE_SIZE * (32 - j)) + SECTOR_SIZE, SEEK_SET);
                    fprintf(flash_memory, "%d", dram.buffer_spare_lsn[j]);
                    fseek(flash_memory, dram.last_use_addr - (PAGE_SIZE * (32 - j)) + SECTOR_SIZE + 8, SEEK_SET);
                    fputs(" -1     ", flash_memory);
                    counter.write_count[counter.command_file_sequence] += 1;                                        // write_counter++ 유효한 데이터 다시 써줌 
                    dram.last_use_addr = (dram.last_use_addr - PAGE_SIZE);
                    fclose(flash_memory);
                }
                else { // 섹터가 무효한 데이터라면 날림
                    flash_memory = fopen("flash_memory.txt", "r+");
                    fseek(flash_memory, dram.last_use_addr - (PAGE_SIZE * (32 - j)), SEEK_SET);
                    fprintf(flash_memory, "-1");
                    for (int j = 0; j < 510; j++) {
                        fprintf(flash_memory, " ");
                    }
                    fseek(flash_memory, dram.last_use_addr - (PAGE_SIZE * (32 - j)) + SECTOR_SIZE, SEEK_SET);
                    fputs("-1       -1     ", flash_memory);
                    fclose(flash_memory);
                }
            }
            counter.erase_count[counter.command_file_sequence] += 1;                                             // erase_counter++ 

        }
    }
    else { // 플래시 메모리 내 모든 섹터가 유효하다면
        flash_memory = fopen("flash_memory.txt", "r+");

        int origin_page_spare_vaild = dram.mapping_table[lsn] * (PAGE_SIZE)+SECTOR_SIZE + 9;
        fseek(flash_memory, origin_page_spare_vaild, SEEK_SET);
        fputs("00  ", flash_memory); // 기존의 자신 데이터를 무효함 표기

        counter.write_count[counter.command_file_sequence] += 1;                                                // write_counter++ 자기 자신 무효 표기 
        fclose(flash_memory);
        erase(flash_memory, lsn);

    }
    return;
}

void write(FILE* flash_memory, int lsn, int psn) {
    if (dram.round_count >= FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE) { // 처음부터 끝까지 다 돌았으면 
        dram.round_count = 0;
        erase(flash_memory, dram.last_use_addr); // 지우기 연산 실행
        dram.round_count = 1;
        write(flash_memory, lsn, dram.last_use_addr);
    }
    else {
        read(flash_memory, psn); // read()로 명시 된 섹터 위치를 읽음

        if (dram.round_count == 0) { // 명령의 첫 read() 실행이면 
            temp.orign_spare_lsn = (int)temp.spare_lsn;
        }

        if (temp.spare_lsn == -1) { // 현재 위치가 사용 가능한(빈) 섹터라면
            flash_memory = fopen("flash_memory.txt", "r+");
            if (flash_memory == NULL) {
                printf("Flash Memory Error in write()..!\n");
            }
            else {
                fseek(flash_memory, psn, SEEK_SET);
                fprintf(flash_memory, "%d", lsn);
                fseek(flash_memory, psn + SECTOR_SIZE, SEEK_SET);
                fprintf(flash_memory, "%d", lsn);
                fseek(flash_memory, psn + SECTOR_SIZE + 8, SEEK_SET);
                fputs(" -1     ", flash_memory);
                dram.mapping_table[lsn] = psn / PAGE_SIZE;
                counter.write_count[counter.command_file_sequence] += 1;  

                if (dram.round_count != 0 && temp.orign_spare_lsn == lsn) { // overwrite이고 기존 위치에 데이터가 자신의 것이라면
                    int origin_page_spare_vaild = dram.mapping_table[lsn] * (PAGE_SIZE)+SECTOR_SIZE + 9;
                    fseek(flash_memory, origin_page_spare_vaild, SEEK_SET);
                    fprintf(flash_memory, "%02d", 0); // 기존에 매핑 된 데이터 무효함을 표기
                    counter.write_count[counter.command_file_sequence] += 1;                                 // write_counter++ 자기 자신 무효 표기 
                    dram.mapping_table[lsn] = psn / PAGE_SIZE;
                }
            }
            fclose(flash_memory);
            return;
        }
        else { // 현재 위치가 사용 중인 섹터라면
            dram.round_count++;
            psn = (psn + PAGE_SIZE) % ((FLASH_MEMORY_SIZE * 1024 * 1024 / SECTOR_SIZE) * PAGE_SIZE); // 다음 섹터의 위치로 이동
            write(flash_memory, lsn, psn);
            return;
        }
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
    int psn = 0;

    counter.command_file_sequence = -1;

    // for (int f = 0; f < max_sequence; f++) {
    //     clock_t start = clock(); // 시작 시간 체크

    //     init(flash_memory);

    //     sprintf(file_name, "%s.txt", command_file_name[f]);
    //     command_file = fopen(file_name, "r");
    //     fseek(command_file, 0L, SEEK_END);
    //     flash_size = ftell(command_file);
    //     fseek(command_file, 0L, SEEK_SET);

    //     while (feof(command_file) == 0) { // 파일 끝까지 읽기
    //         if (ftell(command_file) == flash_size) {
    //             break;
    //         }

    //         lsn = 0;
    //         fscanf(command_file, "%s\t%d", command_type, &lsn);


    //         if (strchr(command_type, 'w')) {
    //             temp.orign_spare_lsn = 0;
    //             dram.round_count = 0;
    //             psn = dram.mapping_table[lsn] * (PAGE_SIZE);
    //             write(flash_memory, lsn, psn);
    //         }
    //     }
    //     fclose(command_file);
    //     clock_t end = clock(); // 끝난 시간 체크
    //     counter.time_required[counter.command_file_sequence] = (double)(end - start) / CLOCKS_PER_SEC;
    // }

    // // Sequence 00 =====================================================
    init(flash_memory);
    command_file = fopen("kodak-pattern.txt", "r");
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

          temp.orign_spare_lsn = 0;
          dram.round_count = 0;
          psn = dram.mapping_table[lsn] * (PAGE_SIZE);
          write(flash_memory, lsn, psn);
       }
    }
    fclose(command_file);

    printf("\n");
    printf("====================================================================================================================================\n");
    printf("Static Sector 5MB\n\n");

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