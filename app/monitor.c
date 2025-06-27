#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define IOCTL_SET_MODE _IOW('p', 1, int)
#define MODE_MONITOR 0

typedef struct {
    char feeding_time[32];      // 사료/물 섭취 시간
    char defecation[4];         // 배변 감지 (YES/NO)
    float temperature;          // 체온
    char sleep_state[4];       // 수면 감지 (YES/NO)
    char activity_time[32];     // 마지막 활동 시간
} PetStatus;


PetStatus info;


int fd;

void print_main_menu() {
    printf("#############################################\n");
    printf("#         Smart Pet Care Monitor            #\n");
    printf("#############################################\n");
    printf("원하는 작업을 선택해주세요.\n");
    printf("1. 정보 모니터링\n");
    printf("2. 정보 갱신\n");
    printf("3. 명령 전달\n");
    printf("0. 종료\n");
    printf("입력 : ");
}

void show_monitoring_info() {
    printf("\n[정보 모니터링 결과]\n");
    printf("사료/물 섭취 시간 : %s\n", info.feeding_time);
    printf("배변 감지 : %s\n", info.defecation);
    printf("체온 : %.1fC\n", info.temperature);
    printf("수면 상태 : %s\n", info.sleep_state);
    printf("행동 시간 : %s\n\n", info.activity_time);
}

void transmit_and_receive(const char* req_msg, char* recv_buf, size_t buf_size) {
    printf("DATA (GPIO 16) : OUT\n");
    printf("CLK (GPIO 17) : OUT\n");
    printf("전송중...\n");
    write(fd, req_msg, strlen(req_msg));
    printf("전송완료!\n");

    printf("DATA (GPIO 16) : IN\n");
    printf("CLK (GPIO 17) : IN\n");
    printf("정보 기다리는 중...\n");
    msleep(50); 
    ssize_t r = read(fd, recv_buf, sizeof(recv_buf));
    if (r < 0) { perror("read 실패"); return; }
    else if (r==0) { perror("read 데이터 0"); return; }
    recv_buf[r] = '\0'; // 안전 조치
    printf("DEBUG: recv_buf = [%s], len = %zd\n", recv_buf, r);

    printf("정보 받기 완료!\n");
}

void request_update() {
    int choice;
    char recv_buf[64] = {0};

    printf("\n갱신하고 싶은 정보 종류를 선택해주세요.\n");
    printf("1. 사료, 물 섭취 시간\n");
    printf("2. 배변 감지\n");
    printf("3. 체온 측정\n");
    printf("4. 수면 상태 모니터링\n");
    printf("5. 행동 시간\n");
    printf("0. 뒤로가기\n");
    printf("입력 : ");
    scanf("%d", &choice);
    getchar();

    if (choice == 0) return;

    char msg[16];
    snprintf(msg, sizeof(msg), "REQ,%02d", choice);
    transmit_and_receive(msg, recv_buf, sizeof(recv_buf));

    switch (choice) {
        case 1: strncpy(info.feeding_time, recv_buf, sizeof(info.feeding_time)); break;
        case 2: strncpy(info.defecation, recv_buf, sizeof(info.defecation)); break;
        case 3: info.temperature = atof(recv_buf); break;
        case 4: strncpy(info.sleep_state, recv_buf, sizeof(info.sleep_state)); break;
        case 5: strncpy(info.activity_time, recv_buf, sizeof(info.activity_time)); break;
    }

    printf("\n[갱신 결과]\n");
    switch (choice) {
        case 1: printf("사료/물 섭취 시간 : %s\n", info.feeding_time); break;
        case 2: printf("배변 감지 : %s\n", info.defecation); break;
        case 3: printf("체온 : %.1f도C\n", info.temperature); break;
        case 4: printf("수면 상태 : %s\n", info.sleep_state); break;
        case 5: printf("행동 시간 : %s\n", info.activity_time); break;
    }
    printf("\n");
}

void send_command() {
    int command;
    char recv_buf[32] = {0};

    printf("\n전달할 명령을 선택해주세요.\n");
    printf("1. 급식기, 급수기 작동\n");
    printf("2. 배변 청소\n");
    printf("3. 레이저 놀아주기 기능\n");
    printf("0. 뒤로가기\n");
    printf("입력 : ");
    scanf("%d", &command);
    getchar();

    if (command == 0) return;

    char msg[16];
    snprintf(msg, sizeof(msg), "CMD,%02d", command);
    transmit_and_receive(msg, recv_buf, sizeof(recv_buf));

    printf("%s\n",recv_buf);
}

int main() {
    int menu;
    fd = open("/dev/spetcom", O_RDWR);
    if (fd < 0) {
        perror("디바이스 열기 실패");
        return 1;
    }

    int mode = MODE_MONITOR;
    ioctl(fd, IOCTL_SET_MODE, &mode);

    while (1) {
        print_main_menu();
        scanf("%d", &menu);
        getchar();

        switch (menu) {
            case 1:
                show_monitoring_info();
                break;
            case 2:
                request_update();
                break;
            case 3:
                send_command();
                break;
            case 0:
                printf("프로그램을 종료합니다.\n");
                close(fd);
                return 0;
            default:
                printf("잘못된 입력입니다. 다시 입력해주세요.\n\n");
        }
    }

    return 0;
}
