export module vui.examples.game2048:game;
import rstd;

export namespace game2048 {
using rstd::uint32_t;
using rstd::uint64_t;
enum class Direction { Left, Right, Up, Down };
struct Board {
    uint32_t cells[16]{};
    uint64_t score{};
};
struct Movement {
    unsigned from{}, to{};
};
struct Spawn {
    unsigned cell{};
    uint32_t value{};
};
struct MoveResult {
    bool changed{};
    Movement movements[16]{};
    unsigned count{};
    bool merged[16]{};
    Spawn spawned;
    uint64_t score_delta{};
};
class Game {
    Board board_;
    uint32_t random_;

    auto random(unsigned bound) -> unsigned {
        uint32_t value;
        do {
            random_ ^= random_ << 13;
            random_ ^= random_ >> 17;
            random_ ^= random_ << 5;
            value = random_ - 1;
        } while (value >= rstd::u32::MAX.to_primitive() - rstd::u32::MAX.to_primitive() % bound);
        return value % bound;
    }
    auto spawn() -> Spawn {
        unsigned empty[16]{}, count{};
        for (unsigned i = 0; i < 16; ++i)
            if (!board_.cells[i])
                empty[count++] = i;
        if (!count)
            return {};
        Spawn result{empty[random(count)], random(10) == 0 ? 4u : 2u};
        board_.cells[result.cell] = result.value;
        return result;
    }
    static auto index(Direction direction, unsigned line, unsigned offset) -> unsigned {
        switch (direction) {
        case Direction::Left:
            return line * 4 + offset;
        case Direction::Right:
            return line * 4 + 3 - offset;
        case Direction::Up:
            return offset * 4 + line;
        case Direction::Down:
            return (3 - offset) * 4 + line;
        }
        return 0;
    }
    static auto can_merge(uint32_t a, uint32_t b) -> bool {
        return a && a == b && a <= rstd::u32::MAX.to_primitive() / 2;
    }

  public:
    explicit Game(uint32_t seed) : random_(seed ? seed : 1) { restart(); }
    Game(Board board, uint32_t seed) : board_(board), random_(seed ? seed : 1) {}
    auto board() const -> const Board & { return board_; }
    void restart() {
        board_ = {};
        spawn();
        spawn();
    }
    auto reached_2048() const -> bool {
        for (auto value : board_.cells)
            if (value >= 2048)
                return true;
        return false;
    }
    auto can_move() const -> bool {
        for (unsigned i = 0; i < 16; ++i) {
            if (!board_.cells[i])
                return true;
            if (i % 4 < 3 && can_merge(board_.cells[i], board_.cells[i + 1]))
                return true;
            if (i < 12 && can_merge(board_.cells[i], board_.cells[i + 4]))
                return true;
        }
        return false;
    }
    auto move(Direction direction) -> MoveResult {
        MoveResult result;
        Board next{};
        next.score = board_.score;
        for (unsigned line = 0; line < 4; ++line) {
            unsigned occupied{};
            for (unsigned offset = 0; offset < 4; ++offset) {
                const auto from = index(direction, line, offset);
                const auto value = board_.cells[from];
                if (!value)
                    continue;
                unsigned to = index(direction, line, occupied ? occupied - 1 : 0);
                if (occupied && !result.merged[to] && can_merge(next.cells[to], value)) {
                    next.cells[to] *= 2;
                    result.score_delta += next.cells[to];
                    result.merged[to] = true;
                } else {
                    to = index(direction, line, occupied++);
                    next.cells[to] = value;
                }
                result.movements[result.count++] = {from, to};
                result.changed |= from != to;
            }
        }
        if (result.changed) {
            next.score += result.score_delta;
            board_ = next;
            result.spawned = spawn();
        }
        return result;
    }
};
} // namespace game2048
