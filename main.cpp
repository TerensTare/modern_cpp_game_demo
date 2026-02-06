
#include <cstdio>
#include <span>

#include <entt/entity/registry.hpp>
#include <SDL3/SDL.h>

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "coro/event.hpp"
#include "coro/scheduler.hpp"
#include "coro/fire_and_forget.hpp"
#include "coro/profiler_gui.hpp"
#include "coro/timeout.hpp"

#include "demo/text.hpp"

// TODO:
// - async file loading
// - task allocator support

#define ENSURE(val, msg)                                     \
    if (!(val))                                              \
    {                                                        \
        std::printf("ERROR: " msg ". %s\n", SDL_GetError()); \
        return -1;                                           \
    }

struct pos final
{
    float x, y;
};

struct speed final
{
    float s; // pixels per second
};

#define NAMED_STAGE(x) []                                         \
{                                                                 \
    return stage_id{entt::hashed_string::value(x, std::size(x))}; \
}()

auto constexpr imgui_stage = NAMED_STAGE("imgui");

// demo: adding Dear ImGui to your game
auto imgui_system(scheduler &sched, context &ctx, SDL_Window *win, SDL_Renderer *ren) -> fire_and_forget
{
    // initialize ImGui during startup
    co_await sched.stages[stage_id::startup].sched();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    while (!sched.stop.stop_requested())
    {
        // build the widgets during the update stage
        co_await sched.stages[stage_id::update].sched();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        sched.stages[imgui_stage].run(ctx);

        ImGui::Render();

        // submit ImGui "draw calls" after everything else so it shows up on top
        co_await sched.stages[stage_id::render].sched();

        // TODO: this must be ran after everything else
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
    }

    // cleanup ImGui when done
    co_await sched.stages[stage_id::cleanup].sched();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

// system that makes the entity positions into a rectangle for rendering
inline void pos_to_rect(entt::registry &reg)
{
    auto const size = 50.0f;

    auto &&rects = reg.storage<SDL_FRect>();
    auto const &positions = reg.storage<pos>();
    for (auto &&[id, p] : positions.each())
    {
        if (!rects.contains(id))
            rects.emplace(id, 0.0f, 0.0f, size, size);
    }

    for (auto &&[id, p, r] : entt::basic_view{positions, rects}.each())
    {
        r.x = p.x - size / 2;
        r.y = p.y + size / 2;
    }
}

auto render_task(scheduler &sched, entt::registry &reg, SDL_Renderer *ren) -> fire_and_forget
{
    while (true)
    {
        co_await sched.stages[stage_id::render].sched();

        pos_to_rect(reg);

        // render boxes
        for (auto &&[id, rect, col] : reg.view<SDL_FRect const, SDL_Color const>().each())
        {
            SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(ren, &rect);
        }

        // render text
        for (auto &&[id, text, pos] : reg.view<detail::unique_text, SDL_FPoint const>().each())
            TTF_DrawRendererText(text.get(), pos.x, pos.y);
    }
}

auto timeout_showcase(scheduler &sched, std::stop_token stop) -> fire_and_forget
{
    // sleep-and-print but also check for cancellations (timeouts)
    co_await sched.stages[stage_id::update].sleep(1000);

    if (stop.stop_requested())
    {
        std::printf("Cancelled\n");
        co_return;
    }

    std::printf("After first sleep\n");

    co_await sched.stages[stage_id::update].sleep(2000);

    if (stop.stop_requested())
    {
        std::printf("Cancelled\n");
        co_return;
    }

    std::printf("After second sleep\n");

    co_await sched.stages[stage_id::update].sleep(3000);

    if (stop.stop_requested())
    {
        std::printf("Cancelled\n");
        co_return;
    }

    std::printf("After third sleep\n");
}

constexpr float lerp(float a, float b, float pct)
{
    return a + (b - a) * pct;
}

// patrol the object around the given center clockwise
auto move_around(scheduler &sched, context &ctx, entt::registry &reg, entt::entity object, pos center, float dist) -> fire_and_forget
{
    // TODO: when mouse clicked, patrol to a new point
    auto &&[p, s] = reg.get<pos, speed const>(object);
    pos const quad[4]{
        {center.x - dist, center.y - dist}, // top-left
        {center.x + dist, center.y - dist}, // top-right
        {center.x + dist, center.y + dist}, // bot-right
        {center.x - dist, center.y + dist}, // bot-left
    };

    // pick the closest point to the current position
    auto which_point = [&]
    {
        auto const dx1 = quad[0].x - p.x;
        auto const dx2 = quad[2].x - p.x;
        auto const dy1 = quad[0].y - p.y;
        auto const dy2 = quad[2].y - p.y;

        auto const x1_less = abs(dx1) < abs(dx2);
        auto const y1_less = abs(dy1) < abs(dy2);

        if (x1_less)
            return y1_less ? 0 : 3;
        else
            return y1_less ? 1 : 2;
    }();

    while (true)
    {
        auto const target = quad[which_point];

        // calculate how long it would take to reach the next patrol point
        auto const len = hypot(target.x - p.x, target.y - p.y);
        auto const elapsed_time = len / (s.s / 1000.0f);

        auto const start_pos = p;
        auto const start_time = ctx.time.now;
        auto const end_time = start_time + elapsed_time;

        auto const inv_elapsed = 1.0f / elapsed_time;

        while (ctx.time.now < end_time)
        {
            co_await sched.stages[stage_id::update].sched();

            // if mouse clicked, patrol around the mouse's position when it was clicked
            if (float mx, my; SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK)
            {
                move_around(sched, ctx, reg, object, pos{mx, my}, dist);
                co_return;
            }

            // calculate progress from start and update position
            auto const t_pct = (ctx.time.now - start_time) * inv_elapsed;

            p = {
                .x = lerp(start_pos.x, target.x, t_pct),
                .y = lerp(start_pos.y, target.y, t_pct),
            };
        }

        // move on to the next point
        which_point = (which_point + 1) % 4;
    }
}

inline entt::entity spawn_player(scheduler &sched, context &ctx, entt::registry &reg)
{
    auto const id = reg.create();
    reg.emplace<SDL_Color>(id) = {0xff, 0x00, 0x00, 0xff};
    reg.emplace<pos>(id) = {500.0f, 500.0f};
    reg.emplace<speed>(id, 100.0f);

    move_around(sched, ctx, reg, id, pos{600.0f, 500.0f}, 100.0f);

    return id;
}

auto change_box_color(scheduler &sched, entt::registry &reg, entt::entity box) -> fire_and_forget
{
    SDL_Color const colors[]{
        {255, 0, 0, 255},
        {255, 127, 0, 255},
        {255, 255, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
        {75, 0, 130, 255},
        {148, 0, 211, 255},
    };

    while (true)
    {
        for (auto c : colors)
        {
            reg.get<SDL_Color>(box) = c;
            co_await sched.stages[stage_id::update].sleep(500);
        }
    }
}

inline entt::entity spawn_color_box(scheduler &sched, entt::registry &reg)
{
    auto const id = reg.create();
    reg.emplace<SDL_FRect>(id, 100.0f, 100.0f, 100.0f, 100.0f);
    reg.emplace<SDL_Color>(id) = {0xff, 0x00, 0x00, 0xff};

    change_box_color(sched, reg, id);

    return id;
}

auto change_box_zoom(scheduler &sched, context &ctx, entt::registry &reg, entt::entity box) -> fire_and_forget
{
    auto const
        zmin = 0.5f,
        zmax = 1.5f,
        zstep = 0.1f;

    auto const &area = reg.get<SDL_FRect>(box);

    auto const
        ow = area.w,             // original w
        oh = area.h,             // original h
        cx = area.x + ow * 0.5f, // original center x
        cy = area.y + oh * 0.5f; // original center y

    float zoom = 1.0f;

    while (true)
    {
        auto const
            start_time = ctx.time.now,
            duration = Uint64(1000),
            end_time = start_time + duration;

        auto smoothstep = [](float x)
        { return x * x * (3 - 2 * x); };

        while (ctx.time.now < end_time)
        {
            co_await sched.stages[stage_id::update].sched();

            auto const
                pct = float(ctx.time.now - start_time) / duration,
                fx = smoothstep(pct),              // [0, 1]
                zero_max_min = fx * (zmax - zmin), // [0, zmax - zmin]
                min_max = zmin + zero_max_min;     // [zmin, zmax]

            zoom = min_max;

            auto const
                w = ow * zoom,
                h = oh * zoom;

            reg.get<SDL_FRect>(box) = {
                .x = cx - w * 0.5f,
                .y = cy - h * 0.5f,
                .w = w,
                .h = h,
            };
        }
    }
}

inline entt::entity spawn_zoom_box(scheduler &sched, context &ctx, entt::registry &reg)
{
    auto const id = reg.create();
    reg.emplace<SDL_FRect>(id, 400.0f, 100.0f, 100.0f, 100.0f);
    reg.emplace<SDL_Color>(id) = {0xff, 0x00, 0x00, 0xff};

    change_box_zoom(sched, ctx, reg, id);

    return id;
}

auto dialogue(dialogue_builder &dlg) -> fire_and_forget
{
    co_await dlg.say("NPC", "Hello player. Do you want to hear a secret?");

    std::string_view secrets[]{
        "Everything in this demo uses coroutines.",
        "The profiler window will show you the runtime of any coroutine type,\nas long as you schedule it into a stage, with no extra setup.",
        "The wandering box will follow you anywhere you click.",
        "There is a coroutine that timed out already and was cancelled, check the console",
        "You can add your own stages to the demo and order them as you want,\nfor example the ImGui stage is not a \"default\" stage.",
    };
    size_t const n_secrets = std::size(secrets);

    // shuffle our secrets for extra spice :)
    for (rsize_t i = n_secrets - 1; i > 0; --i)
        std::swap(secrets[i], secrets[SDL_rand(i + 1)]);

    size_t current_secret{};

    while (true)
    {
        std::string_view constexpr options[]{"Yes", "No", "Maybe"};
        uint32_t choice = co_await dlg.choose(std::span(options));

#define alt(x) std::find(options, options + std::size(options), x) - options

        switch (choice)
        {
        case alt("Yes"):
            co_await dlg.say("NPC", secrets[current_secret]);
            co_await dlg.say("NPC", "Do you want to hear another secret?");
            current_secret = (current_secret + 1) % n_secrets;
            break;

        case alt("No"):
            co_await dlg.say("NPC", "Ok, never mind :)");
            co_return;

        case alt("Maybe"):
            co_await dlg.say("NPC", "I need a definite answer!");
            break;
        }

#undef alt
    }
}

auto imgui_widgets(scheduler &sched, context &ctx) -> fire_and_forget
{
    while (true)
    {
        co_await sched.stages[imgui_stage].sched();

        coroutine_profiler(ctx.traces);
    }
}

int main(int, char **)
{
    auto init_sdl = SDL_Init(SDL_INIT_VIDEO);
    ENSURE(init_sdl, "Couldn't init SDL3");

    auto win_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    auto win = SDL_CreateWindow("Modern C++ game example", 1280, 720, win_flags);
    ENSURE(win, "Couldn't create window");

    auto ren = SDL_CreateRenderer(win, nullptr);
    ENSURE(ren, "Couldn't create renderer");

    auto init_ttf = TTF_Init();
    ENSURE(init_ttf, "Couldn't init SDL ttf");

    auto text_engine = TTF_CreateRendererTextEngine(ren);
    ENSURE(text_engine, "Couldn't create text engine");

    auto font = TTF_OpenFont("assets/fonts/Exo_2/static/Exo2-Regular.ttf", 24.0f);
    ENSURE(font, "Couldn't open font");

    entt::registry reg;

    // declare the scheduler + context
    scheduler sched;
    context ctx;

    dialogue_builder dlg{
        .sched = &sched,
        .ctx = &ctx,
        .reg = &reg,
        .eng = text_engine,
        .font = font,
        .position = {100.0f, 300.0f},
    };

    // create all the coroutines you plan to submit initially
    imgui_system(sched, ctx, win, ren); // this handles ImGui setup + cleanup
    imgui_widgets(sched, ctx);          // this handles the widgets

    render_task(sched, reg, ren);
    dialogue(dlg);

    timeout_showcase(
        sched,
        timeout(sched.stages[stage_id::update], 3500) //
    );

    // spawn some entities
    spawn_player(sched, ctx, reg);
    spawn_color_box(sched, reg);
    spawn_zoom_box(sched, ctx, reg);

    // run the startup stage
    sched.stages[stage_id::startup].run(ctx);

    while (!sched.stop.stop_requested())
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                sched.stop.request_stop();
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(win))
                    sched.stop.request_stop();
                break;
            }
        }

        // game loop
        sched.stages[stage_id::update].run(ctx); // first, run the "tick" coroutines (game logic)

        // rendering
        {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);

            sched.stages[stage_id::render].run(ctx); // then run rendering coroutines

            SDL_RenderPresent(ren);
        }

        SDL_Delay(1);
    }

    sched.stages[stage_id::cleanup].run(ctx); // finally run cleanup-related coros

    TTF_CloseFont(font);
    TTF_DestroyRendererTextEngine(text_engine);
    TTF_Quit();

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    return 0;
}