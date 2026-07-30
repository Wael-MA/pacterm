#include <iostream>
#include "Types.hpp"

int main() {
    Map map;
    map.loadLevel(1);
    
    std::cout << "Grid Tiles around col 20-27, row 1-5:\n";
    for (int y = 1; y <= 5; ++y) {
        std::cout << "Row " << y << ": ";
        for (int x = 20; x < 28; ++x) {
            TileType t = map.getTile(x, y);
            char c = '?';
            if (t == TileType::Empty) c = ' ';
            else if (t == TileType::Wall) c = '#';
            else if (t == TileType::Dot) c = '.';
            else if (t == TileType::PowerPellet) c = 'o';
            else if (t == TileType::Fruit) c = 'F';
            else if (t == TileType::Cherry) c = 'C';
            else if (t == TileType::GoldenApple) c = 'A';
            else if (t == TileType::LetterP) c = 'P';
            else if (t == TileType::LetterA) c = 'a';
            else if (t == TileType::LetterC) c = 'c';
            else if (t == TileType::LetterT) c = 't';
            else if (t == TileType::LetterE) c = 'e';
            else if (t == TileType::LetterR) c = 'r';
            else if (t == TileType::LetterM) c = 'm';
            std::cout << c;
        }
        std::cout << "\n";
    }
    return 0;
}
