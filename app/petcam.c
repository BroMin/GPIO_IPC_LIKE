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
    char sleep_state[16];
    char activity_time[32];
} PetStatus;

PetStatus info = {
    .feeding_time = "2025-06-24 08:00",
    .defecation = "Y",
    .temperature = 38.5,
    .sleep_state = "SLEEP",
    .activity_time = "2025-06-24 07:30"
};

void handle_request(const char* req, char* resp) {
    pthread_mutex_lock(&lock);
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
                snprintf(resp, 32, "OK");
                time_t t = time(NULL);
                strftime(info.feeding_time, sizeof(info.feeding_time), "%Y-%m-%d %H:%M", localtime(&t));
                break;
            case 2:
                snprintf(resp, 32, "OK");
                strcpy(info.defecation, "N");
                break;
            case 3:
                snprintf(resp, 32, "OK");
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
        char input[64];
        printf("[입력 예시] 번호 [값] → 예: 2 Y 또는 3 37.5, 1 또는 5 입력 시 현재 시간 자동\n");
        printf("센싱 데이터 입력 (1~5): ");
        int count = scanf("%d %63s", &field, input);
        if (count < 1) {
            printf("입력 오류. 다시 시도하세요.\n");
            while (getchar() != '\n'); // flush stdin
            continue;
        }

        pthread_mutex_lock(&lock);
        time_t now;
        switch (field) {
            case 1:
                now = time(NULL);
                strftime(info.feeding_time, sizeof(info.feeding_time), "%Y-%m-%d %H:%M", localtime(&now));
                printf("[입력됨] feeding_time = %s\n", info.feeding_time);
                break;
            case 2:
                if (count == 2) strncpy(info.defecation, input, sizeof(info.defecation));
                break;
            case 3:
                if (count == 2) info.temperature = atof(input);
                break;
            case 4:
                if (count == 2) strncpy(info.sleep_state, input, sizeof(info.sleep_state));
                break;
            case 5:
                now = time(NULL);
                strftime(info.activity_time, sizeof(info.activity_time), "%Y-%m-%d %H:%M", localtime(&now));
                printf("[입력됨] activity_time = %s\n", info.activity_time);
                break;
            default:
                printf("잘못된 항목 번호입니다.\n");
                break;
        }
        pthread_mutex_unlock(&lock);
        while (getchar() != '\n'); // flush remainder
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
        printf("[petcam] 요청 대기 중...\n");
        read(fd, req_buf, sizeof(req_buf));
        printf("[petcam] 수신된 요청: %s\n", req_buf);

        handle_request(req_buf, resp_buf);

        write(fd, resp_buf, strlen(resp_buf));
        printf("[petcam] 응답 전송 완료: %s\n\n", resp_buf);
        memset(req_buf, 0, sizeof(req_buf));
        memset(resp_buf, 0, sizeof(resp_buf));
    }

    pthread_mutex_destroy(&lock);
    close(fd);
    return 0;
}