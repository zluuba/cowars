#include "raylib.h"
#include "raymath.h"
#include "util.h"
#include "server.h"
#include "client.h"

#include <pthread.h>
#include <string.h>
#include <errno.h>

#define SCREEN_WIDTH 400
#define SCREEN_HEIGHT 240
#define LINE_THICKNESS 8.0f
#define SERVER_PORT 12345

struct DrawLineMsg {
    Vector2 start;
    Vector2 end;
    Color draw_color;
};

int server_cb(
    struct ServerRunningCtx *server, struct ServerClient *client, struct TlvMsg *msg, void *cb_ctx
) {
    return server_send_to_other(server, client, msg);
}

void* main_server(void *) {
    logenter();
    struct ServerCfg server_cfg = {
        .max_events = 100,
        .max_clients = 8,
        .port = SERVER_PORT,
        .cb_ctx = NULL,
        .msg_default_cb = server_cb
    };
    struct Server server = {};
    rci(server_init(&server_cfg, &server), NULL, "init server");
    rcci(server_run(&server), server_deinit(&server), NULL, "run reserver");
    server_deinit(&server);
    return NULL;
}

int client_cb(struct Client *client, struct TlvMsg *msg, void *cb_ctx) {
    RenderTexture *canvas = (RenderTexture *)cb_ctx;
    struct DrawLineMsg draw_msg = *(struct DrawLineMsg *)msg->val;
    BeginTextureMode(*canvas);
        DrawLineEx(draw_msg.start, draw_msg.end, LINE_THICKNESS*1.5, BLACK);
        DrawCircleV(draw_msg.start, LINE_THICKNESS/2.0f, draw_msg.draw_color);
        DrawCircleV(draw_msg.end, LINE_THICKNESS/2.0f, draw_msg.draw_color);
        DrawLineEx(draw_msg.start, draw_msg.end, LINE_THICKNESS, draw_msg.draw_color);
    EndTextureMode();
    return 0;
}

int main(int argc, char *argv[]) {
    pthread_t tid;
    int is_server = 0;
    if (argc == 1) {
    } else if (argc == 2 && !strcmp(argv[1], "server")) {
        is_server = 1;
        rei(pthread_create(&tid, NULL, main_server, NULL),
             "create server thread: %s", strerror(errno));
    } else {
        loge("Usage: %s [server]\n", argv[0]);
        return -1;
    }

    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, is_server ? "Server" : "Client");

    Vector2 mouse_prev_pos = GetMousePosition();
    RenderTexture canvas = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    float line_hue = 0.0f;

    struct ClientCfg client_cfg = {
        .server_port = SERVER_PORT,
        .server_ip = {127, 0, 0, 1},
        .cb_ctx = &canvas,
        .msg_default_cb = client_cb
    };
    struct Client client = {};
    rei(client_init(&client_cfg, &client), "init client");

    BeginTextureMode(canvas);
        ClearBackground(RAYWHITE);
    EndTextureMode();

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            BeginTextureMode(canvas);
                ClearBackground(RAYWHITE);
            EndTextureMode();
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float dist = Vector2Distance(mouse_prev_pos, GetMousePosition()) / 3.0f;
            if (dist > 0.1f) {
                line_hue = fmodf(line_hue + dist, 360.0f);
                Color draw_color = ColorFromHSV(line_hue, 1.0f, 1.0f);
                BeginTextureMode(canvas);
                    DrawLineEx(mouse_prev_pos, GetMousePosition(), LINE_THICKNESS*1.5, BLACK);
                    DrawCircleV(mouse_prev_pos, LINE_THICKNESS/2.0f, draw_color);
                    DrawCircleV(GetMousePosition(), LINE_THICKNESS/2.0f, draw_color);
                    DrawLineEx(mouse_prev_pos, GetMousePosition(), LINE_THICKNESS, draw_color);
                    struct DrawLineMsg draw_msg = {
                        .start = mouse_prev_pos,
                        .end = GetMousePosition(),
                        .draw_color = draw_color
                    };
                    struct TlvMsg msg = {
                        .tag = 0,
                        .len = sizeof(draw_msg),
                        .val = (uint8_t *)&draw_msg
                    };
                    rei(client_send(&client, &msg), "send udp msg");
                EndTextureMode();
            }
        }
        rei(client_update(&client), "update client");
        mouse_prev_pos = GetMousePosition();
        BeginDrawing();
            DrawTextureRec(
                canvas.texture,
                (Rectangle) {
                     0.0f, 0.0f, (float)canvas.texture.width,(float)-canvas.texture.height
                },
                Vector2Zero(),
                WHITE
            );
            DrawCircleLinesV(
                GetMousePosition(),
                LINE_THICKNESS/2.0f,
                (Color){ 127, 127, 127, 127 }
            );
        EndDrawing();
    }
    UnloadRenderTexture(canvas);
    if (is_server) {
        pthread_cancel(tid);
    }
    client_deinit(&client);
    CloseWindow();
    return 0;
}
