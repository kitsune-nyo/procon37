#include <vector>

struct Spot { int pos, brand, maxStock, stock; };

class SpotManager {
public:
    std::vector<Spot> spots;

    void placeSpot(int brand, int pos, int stocks) {
        Spot sp = { brand, pos, stocks, stocks };
        spots.push_back(sp);
    }

    Spot* findAt(int pos) {
        for (Spot& s : spots) if (s.pos == pos) return &s;
        return nullptr;
    }

    bool consume(int pos) {
        Spot* s = findAt(pos);
        if (!s) return false;
        if (s->stock <= 0) return false;
        s->stock--;
        return true;
    }
};