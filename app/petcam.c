#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>

#define IOCTL_SET_MODE _IOW('p', 1, int)
#define MODE_PETCAM 1

pthread_mutex_t lock;

typedef struct {
    char feeding_time[32];
    char defecation[4];
    float temperature;
    char sleep_state[4];
    char activity_time[32];
} PetStatus;

PetStatus info = {
    .feeding_time = "2025-01-01 08:00",
    .defecation = "NO",
    .temperature = 38.5,
    .sleep_state = "YES",
    .activity_time = "2025-01-01 09:30"
};

void handle_request(const char* req, char* resp) {
    pthread_mutex_lock(&lock);
    printf("[handle_request] %s",req);
    if (strncmp(req, "REQ,", 4) == 0) {
        int code = atoi(req + 4);
        switch (code) {
            case 1: snprintf(resp, 64, "%s", info.feeding_time); break;
            case 2: snprintf(resp, 64, "%s", info.defecation); break;
            case 3: snprintf(resp, 64, "%.1f", info.temperature); break;
            case 4: snprintf(resp, 64, "%s", info.sleep_state); break;
            case 5: snprintf(resp, 64, "%s", info.activity_time); break;
            default: snprintf(resp, 64, "INVALID"); break;
        }
    } else if (strncmp(req, "CMD,", 4) == 0) {
        int cmd = atoi(req + 4);
        switch (cmd) {
            case 1:
                snprintf(resp, 32, "[petcam] 급식기, 급수기 작동 완료");
                time_t t = time(NULL);
                strftime(info.feeding_time, sizeof(info.feeding_time), "%Y-%m-%d %H:%M", localtime(&t));
                break;
            case 2:
                snprintf(resp, 32, "[petcam] 배변 청소 완료");
                strcpy(info.defecation, "NO");
                break;
            case 3:
                snprintf(resp, 32, "[petcam] 레이저 놀아주기 완료");
                time_t a = time(NULL);
                strftime(info.activity_time, sizeof(info.activity_time), "%Y-%m-%d %H:%M", localtime(&a));
                break;
            default:
                snprintf(resp, 32, "INVALID");
                break;
        }
    } else {
        snprintf(resp, 32, "UNKNOWN");
    }
    pthread_mutex_unlock(&lock);
}

void* sensing_input_thread(void* arg) {
    while (1) {
        int field;
        printf("#############################################\n");
        printf("#         Smart Pet Care Petcam             #\n");
        printf("#############################################\n");
        printf("센싱 데이터 목록\n");
        printf("1. 음식 / 물 섭취 (시간 저장)\n");
        printf("2. 배변 감지 (Y/N 입력)\n");
        printf("3. 체온 감지 (온도 입력)\n");
        printf("4. 수면 감지 (Y/N 입력)\n");
        printf("5. 움직임 감지 (시간 저장)\n");
        printf("센싱 데이터 번호 입력 (1~5): ");
        
        if (scanf("%d", &field) != 1 || field < 1 || field > 5) {
            printf("잘못된 입력입니다. 다시 시도하세요.\n");
            while (getchar() != '\n'); // flush
            continue;
        }

        pthread_mutex_lock(&lock);
        time_t now;
        switch (field) {
            case 1:
                now = time(NULL);
                strftime(info.feeding_time, sizeof(info.feeding_time), "%Y-%m-%d %H:%M", localtime(&now));
                printf("[감지됨]] feeding_time = %s\n", info.feeding_time);
                break;
            case 2: {
                char yn[4];
                printf("배변 감지 여부 (YES/NO): ");
                scanf("%3s", yn);
                strncpy(info.defecation, yn, sizeof(info.defecation));
                printf("[감지됨] defecation = %s\n", info.defecation);
                break;
            }
            case 3: {
                float temp;
                printf("체온 입력 (ex: 38.2): ");
                scanf("%f", &temp);
                info.temperature = temp;
                printf("[감지됨] temperature = %.1f\n", info.temperature);
                break;
            }
            case 4: {
                char yn[4];
                printf("수면 상태 입력 (YES/NO): ");
                scanf("%3s", yn);
                strncpy(info.sleep_state, yn, sizeof(info.sleep_state));
                printf("[감지됨] sleep_state = %s\n", info.sleep_state);
                break;
            }
            case 5:
                now = time(NULL);
                strftime(info.activity_time, sizeof(info.activity_time), "%Y-%m-%d %H:%M", localtime(&now));
                printf("[감지됨] activity_time = %s\n", info.activity_time);
                break;
        }
        pthread_mutex_unlock(&lock);

        while (getchar() != '\n'); // flush stdin
    }
    return NULL;
}



int main() {
    int fd = open("/dev/spetcom", O_RDWR);
    if (fd < 0) {
        perror("디바이스 열기 실패");
        return 1;
    }

    int mode = MODE_PETCAM;
    ioctl(fd, IOCTL_SET_MODE, &mode);
    pthread_mutex_init(&lock, NULL);

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, sensing_input_thread, NULL);

    char req_buf[64] = {0};
    char resp_buf[64] = {0};

    while (1) {
        printf("[main] 요청 대기 중...\n");
        read(fd, req_buf, sizeof(req_buf));
        printf("[main] 수신된 요청: %s\n", req_buf);

        handle_request(req_buf, resp_buf);

        write(fd, resp_buf, strlen(resp_buf));
        printf("[main] 응답 전송 완료: %s\n\n", resp_buf);
        memset(req_buf, 0, sizeof(req_buf));
        memset(resp_buf, 0, sizeof(resp_buf));
    }

    pthread_mutex_destroy(&lock);
    close(fd);
    return 0;
}