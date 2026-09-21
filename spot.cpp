#include <algorithm>
#include <random>
#include "map.cpp"

struct Spot { int pos, brand, maxStock, stock; };

class SpotManager {
public:
    std::vector<Spot> spots;

    void placeSpotsRandomly(const Map& map, int spotCount, int brandCount,
                     int agentCount, int maxStockCap, unsigned seed = 42) {
        if (spotCount < agentCount)
            throw std::runtime_error("スポット数はエージェント数以上である必要があります");
        if (spotCount > map.width * map.height)
            throw std::runtime_error("スポット数がマップサイズを超えています");
        if (brandCount < 1 || brandCount > spotCount)
            throw std::runtime_error("系列数は1以上スポット数以下である必要があります");

        std::vector<int> plainCells;
        for (int i = 0; i < map.width * map.height; ++i) {
            if (map.terrainAt(i) == Terrain::PLAIN) plainCells.push_back(i);
        }
        if ((int)plainCells.size() < spotCount)
            throw std::runtime_error("平地セルが不足しています");

        std::mt19937 rng(seed);
        std::shuffle(plainCells.begin(), plainCells.end(), rng);

        spots.clear();
        std::uniform_int_distribution<int> brandDist(0, brandCount - 1);
        std::uniform_int_distribution<int> stockDist(1, maxStockCap);

        for (int i = 0; i < spotCount; ++i) {
            Spot sp;
            sp.pos = plainCells[i];
            sp.brand = brandDist(rng);
            sp.maxStock = stockDist(rng);
            sp.stock = sp.maxStock;
            spots.push_back(sp);
        }
    }

    void placeSpot(int brand, int pos, int stocks) {
        Spot sp;
        sp.brand = brand;
        sp.pos = pos;
        sp.maxStock = stocks;
        sp.stock = stocks;
        spots.push_back(sp);
    }

    Spot* findAt(int pos) {
        for (auto& s : spots) if (s.pos == pos) return &s;
        return nullptr;
    }

    bool consume(int pos) {
        Spot* s = findAt(pos);
        if (!s) return false;
        if (s->stock <= 0) return false;
        s->stock--;
        return true;
    }

    void refillAll() { for (auto& s : spots) s.stock = s.maxStock; }
};