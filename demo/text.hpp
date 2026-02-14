
#pragma once

#include <string>
#include <SDL3_ttf/SDL_ttf.h>

#include "coro/scheduler.hpp"
#include "coro/task.hpp"

struct dialogue_text_tag final
{
};

struct text_data final
{
    TTF_Text *text;
};

struct dialogue_builder final
{
    task<> say(std::string_view who, std::string_view what);
    task<uint32_t> choose(std::span<std::string_view const> options);

    inline void cleanup()
    {
        auto &&pool = reg->storage<dialogue_text_tag const>();
        reg->destroy(pool.begin(), pool.end());

        position = origin;
    }

    scheduler *sched;
    context *ctx;
    entt::registry *reg;
    TTF_TextEngine *eng;
    TTF_Font *font;
    SDL_FPoint origin;
    SDL_FPoint position = origin; // internal
};

task<> dialogue_builder::say(std::string_view who, std::string_view what)
{
    // TODO: as a bonus, do longer pauses on punctuations.

    // invariant: the components might not be stable (storage) but the internal pointer to the text (ie. TTF_Text *) is
    auto text = TTF_CreateText(eng, font, who.data(), who.size());
    TTF_AppendTextString(text, ": ", 2);
    // show who says the message + `: `

    auto const id = reg->create();
    reg->emplace<SDL_FPoint>(id, position);
    reg->emplace<text_data>(id, text);
    reg->emplace<dialogue_text_tag>(id);

    std::string message{what};

    Uint64 const
        msg_len = message.size(),
        time_per_letter = 40,
        duration = time_per_letter * message.size(),
        start_time = ctx->time.now,
        end_time = start_time + duration;

    auto last_progress = 0;

    while (ctx->time.now < end_time)
    {
        co_await sched->stages[stage_id::update].sleep(time_per_letter);

        auto const pct = float(ctx->time.now - start_time) / duration;

        // TODO: use a better function
        auto const progress = int(pct * msg_len);
        if (progress > last_progress)
        {
            TTF_AppendTextString(text, message.data() + last_progress, progress - last_progress);
            last_progress = progress;
        }
    }

    // just making sure; this handles newlines in text
    int w, h;
    TTF_GetTextSize(text, &w, &h);
    position.y = reg->get<SDL_FPoint>(id).y + h;
}

task<uint32_t> dialogue_builder::choose(std::span<std::string_view const> options)
{
    uint32_t which = 0;

    auto const n_options = options.size();

    // invariant: the components might not be stable (storage) but the internal pointer to the text (ie. TTF_Text *) is
    std::vector<TTF_Text *> options_text;
    options_text.reserve(n_options);

    auto const old_x = position.x;
    int w, h;

    for (auto opt : options)
    {
        auto text = TTF_CreateText(eng, font, opt.data(), opt.size());

        auto const id = reg->create();
        reg->emplace<SDL_FPoint>(id, position);
        reg->emplace<text_data>(id, text);
        reg->emplace<dialogue_text_tag>(id);

        // update the "cursor" position
        TTF_GetTextSize(text, &w, &h);
        position.x += w;
        position.x += 10.0f; // spacing

        options_text.push_back(text);
    }

    // make the first text red to denote the current choice
    TTF_SetTextColor(options_text[0], 0xff, 0, 0, 0xff);

    position.x = old_x;
    position.y += h;

    while (true)
    {
        co_await sched->stages[stage_id::update].sched();

        if (auto kb = SDL_GetKeyboardState(nullptr);
            kb[SDL_SCANCODE_RIGHT])
        {
            TTF_SetTextColor(options_text[which], 0xff, 0xff, 0xff, 0xff);
            // TODO: you can also stop at last
            which = (which + 1) % n_options;
            TTF_SetTextColor(options_text[which], 0xff, 0, 0, 0xff);
        }
        else if (kb[SDL_SCANCODE_LEFT])
        {
            TTF_SetTextColor(options_text[which], 0xff, 0xff, 0xff, 0xff);
            // TODO: you can also stop at first
            which = (which + n_options - 1) % n_options;
            TTF_SetTextColor(options_text[which], 0xff, 0, 0, 0xff);
        }
        else if (kb[SDL_SCANCODE_RETURN])
        {
            co_return which;
        }
    }
}