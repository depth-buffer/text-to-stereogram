#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <getopt.h>

#include <SDL.h>
#include <SDL_ttf.h>

SDL_Renderer * renderer = nullptr;
SDL_Surface * textsurface = nullptr;
SDL_Texture * texture = nullptr;
TTF_Font * font = nullptr;

void destroy_texture()
{
    SDL_DestroyTexture(texture);
}

void free_textsurface()
{
    SDL_FreeSurface(textsurface);
}

void destroy_renderer()
{
    SDL_DestroyRenderer(renderer);
}

void close_font()
{
    TTF_CloseFont(font);
}

void usage()
{
    std::cerr << "Usage: text-to-stereogram -f <font> -t <tile> [-w <width>] [-h <height>] [-o <output file>] [<string>]\n";
}

int main(int argc, char * const * argv)
{
    int w = 640;
    int h = 480;
    int s = 24;
    char const * fontname = nullptr;
    char const * tilename = nullptr;
    char const * outfname = nullptr;
    char const * text = "Hello, world!";

    {
        int c;
        while ((c = getopt(argc, argv, "w:h:f:s:t:o:")) != -1)
        {
            switch (c)
            {
                case 'w':
                    w = std::atoi(optarg);
                    break;
                case 'h':
                    h = std::atoi(optarg);
                    break;
                case 's':
                    s = std::atoi(optarg);
                    break;
                case 'f':
                    fontname = optarg;
                    break;
                case 't':
                    tilename = optarg;
                    break;
                case 'o':
                    outfname = optarg;
                    break;
                default:
                    usage();
                    return 1;
            }
        }
    }
    if ((fontname == nullptr) || (tilename == nullptr) || (w <= 0) || (h <= 0) || (s <= 0))
    {
        usage();
        return 1;
    }
    if (optind < argc)
        text = argv[optind];

    if (TTF_Init() != 0)
    {
        std::cerr << "Unable to initialise SDL_ttf: " << TTF_GetError() << std::endl;
        return 1;
    }
    std::atexit(TTF_Quit);

    if (!(font = TTF_OpenFont(fontname, s)))
    {
        std::cerr << "Unable to open font: " << TTF_GetError() << std::endl;
        return 1;
    }
    std::atexit(close_font);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "Unable to initialise SDL: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(SDL_Quit);

    auto window = SDL_CreateWindow(
            "text-to-stereogram",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            w, h, SDL_WINDOW_SHOWN);
    if (!window)
    {
        std::cerr << "Unable to create window: " << SDL_GetError() << std::endl;
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
    {
        std::cerr << "Unable to create renderer: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(destroy_renderer);

    textsurface = TTF_RenderUTF8_Shaded(font, text, {255, 255, 255}, {0, 0, 0});
    if (!textsurface)
    {
        std::cerr << "Unable to render text surface: " << TTF_GetError() << std::endl;
    }
    std::atexit(free_textsurface);

    texture = SDL_CreateTextureFromSurface(renderer, textsurface);
    if (!texture)
    {
        std::cerr << "Unable to create texture from surface: " << SDL_GetError() << std::endl;
    }
    std::atexit(destroy_texture);

    bool quit = false;
    while (!quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
                case SDL_QUIT:
                    quit = true;
            }
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        }
    }

    return 0;
}
