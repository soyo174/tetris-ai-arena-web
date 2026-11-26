// TETRA ARENA C++20 baseline bot (v0.5.2 / reference-realtime-v5.2).
// Single-file, dependency-free, ISO C++20 (compiles with g++ -std=c++20 -O2 and MSVC /std:c++20 /O2).
// Protocol: tetris-duel-jsonl-v1 (stdin/stdout JSONL).

#ifndef BOT_TIER
#define BOT_TIER 0 // Generated from bots/cpp/baseline_core.cpp (weak tier)
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tetra {

using Clock = std::chrono::steady_clock;

constexpr uint16_t FULL_ROW = 0x3FFu;
constexpr int BOARD_W = 10;
constexpr int BOARD_H = 24;
constexpr int NUM_PIECES = 7;

enum Piece : int8_t { P_NONE = -1, P_I = 0, P_O = 1, P_T = 2, P_S = 3, P_Z = 4, P_J = 5, P_L = 6 };
enum SpinKind : uint8_t { SPIN_NONE = 0, SPIN_MINI = 1, SPIN_FULL = 2 };
enum Cmd : uint8_t { CMD_LEFT = 0, CMD_RIGHT = 1, CMD_DOWN = 2, CMD_CW = 3, CMD_CCW = 4, CMD_ROT180 = 5, CMD_HARD_DROP = 6 };

constexpr const char* CMD_NAMES[7] = {
    "LEFT", "RIGHT", "DOWN", "CW", "CCW", "ROT180", "HARD_DROP"
};

inline int8_t char_to_piece(char c) {
    switch (c) {
        case 'I': return P_I;
        case 'O': return P_O;
        case 'T': return P_T;
        case 'S': return P_S;
        case 'Z': return P_Z;
        case 'J': return P_J;
        case 'L': return P_L;
        default: return P_NONE;
    }
}

// ------------------------- Minimal Fast JSON Parser -------------------------

struct Json {
    enum Type : uint8_t { NUL, BOOL, NUM, STR, ARR, OBJ };
    Type type = NUL;
    bool bval = false;
    double nval = 0.0;
    int64_t ival = 0;
    std::string sval;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    const Json* get(std::string_view key) const {
        if (type != OBJ) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
    int as_int(int def = 0) const {
        return type == NUM ? static_cast<int>(ival) : def;
    }
    int64_t as_i64(int64_t def = 0) const {
        return type == NUM ? ival : def;
    }
    bool as_bool(bool def = false) const {
        return type == BOOL ? bval : def;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : s_(src) {}

    bool parse(Json& out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        return pos_ == s_.size();
    }

private:
    std::string_view s_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size() && static_cast<unsigned char>(s_[pos_]) <= ' ') ++pos_;
    }

    bool parse_value(Json& out) {
        skip_ws();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parse_obj(out);
        if (c == '[') return parse_arr(out);
        if (c == '"') {
            out.type = Json::STR;
            return parse_str(out.sval);
        }
        if (c == 't' && s_.substr(pos_, 4) == "true") {
            pos_ += 4;
            out.type = Json::BOOL;
            out.bval = true;
            return true;
        }
        if (c == 'f' && s_.substr(pos_, 5) == "false") {
            pos_ += 5;
            out.type = Json::BOOL;
            out.bval = false;
            return true;
        }
        if (c == 'n' && s_.substr(pos_, 4) == "null") {
            pos_ += 4;
            out.type = Json::NUL;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_num(out);
        return false;
    }

    bool parse_str(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_++];
                switch (e) {
                    case '"': case '\\': case '/': out.push_back(e); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) return false;
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = s_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(10 + h - 'a');
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(10 + h - 'A');
                            else return false;
                        }
                        if (cp < 0x80) out.push_back(static_cast<char>(cp));
                        else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parse_num(Json& out) {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return false;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        bool is_float = false;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            is_float = true;
            ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        std::string tmp(s_.substr(start, pos_ - start));
        out.type = Json::NUM;
        if (!is_float) {
            out.ival = std::strtoll(tmp.c_str(), nullptr, 10);
            out.nval = static_cast<double>(out.ival);
        } else {
            out.nval = std::strtod(tmp.c_str(), nullptr);
            out.ival = static_cast<int64_t>(out.nval);
        }
        return true;
    }

    bool parse_arr(Json& out) {
        ++pos_; // '['
        out.type = Json::ARR;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            Json elem;
            if (!parse_value(elem)) return false;
            out.arr.push_back(std::move(elem));
            skip_ws();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ']') { ++pos_; return true; }
            if (s_[pos_] != ',') return false;
            ++pos_;
        }
        return false;
    }

    bool parse_obj(Json& out) {
        ++pos_; // '{'
        out.type = Json::OBJ;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            skip_ws();
            std::string key;
            if (!parse_str(key)) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            Json val;
            if (!parse_value(val)) return false;
            out.obj.emplace_back(std::move(key), std::move(val));
            skip_ws();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == '}') { ++pos_; return true; }
            if (s_[pos_] != ',') return false;
            ++pos_;
        }
        return false;
    }
};

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

// ------------------------- Rules & Geometry -------------------------

struct Offset {
    int8_t dx = 0;
    int8_t dy = 0;
};

struct KickList {
    std::array<Offset, 8> data{};
    uint8_t count = 0;
};

struct RulesConfig {
    bool is_realtime = true;
    bool has_delayed_locks = true;
    std::array<int, 5> normal_attack = {0, 0, 1, 2, 4};
    std::array<int, 4> tspin_attack = {0, 2, 4, 6};
    int b2b_bonus = 1;
    int perfect_clear_floor = 10;
    int max_commands = 512;
    int garbage_cap = 8; // -1 means unlimited
    int move_frames = 2;
    int rotate_frames = 1;
    int soft_drop_frames = 2;
    int hold_frames = 2;
    int lock_frames = 8;
    int clear_frames = 12;
    int special_clear_frames = 16;
    int free_think_ms = 30;
    int garbage_delay_frames = 30;
    int combo_block_limit = 3;
    int overtime_first_frame = 3600;
    int overtime_interval_frames = 240;
    int max_frames = 10800;
    int overtime_first_turn = 150;
    int overtime_interval_turn = 8;

    std::array<int8_t, NUM_PIECES> spawn_x = {3, 3, 3, 3, 3, 3, 3};
    std::array<int8_t, NUM_PIECES> spawn_y = {18, 19, 19, 19, 19, 19, 19};

    // cells[piece][rot][0..3]
    std::array<std::array<std::array<Offset, 4>, 4>, NUM_PIECES> cells{};
    // Precomputed bounding boxes for fast fits()
    std::array<std::array<int8_t, 4>, NUM_PIECES> min_dx{}, max_dx{}, min_dy{}, max_dy{};
    // kicks90[piece][from_rot][dir] dir: 0 = CW (+1), 1 = CCW (-1)
    std::array<std::array<std::array<KickList, 2>, 4>, NUM_PIECES> kicks90{};
    // kicks180[from_rot]
    std::array<KickList, 4> kicks180{};
    // Precomputed spawn row masks for instant spawn checks
    struct SpawnMask {
        std::array<std::pair<uint8_t, uint16_t>, 4> rows{};
        uint8_t count = 0;
        bool valid = true;
    };
    std::array<SpawnMask, NUM_PIECES> spawn_masks{};
    std::array<bool, NUM_PIECES> unique_rot1{}; // true for O (only 1 distinct orientation)

    RulesConfig() { init_defaults(); }

    void init_defaults() {
        constexpr Offset spawn_cells[NUM_PIECES][4] = {
            {{0,2},{1,2},{2,2},{3,2}}, // I
            {{1,1},{2,1},{1,2},{2,2}}, // O
            {{0,1},{1,1},{2,1},{1,2}}, // T
            {{0,1},{1,1},{1,2},{2,2}}, // S
            {{1,1},{2,1},{0,2},{1,2}}, // Z
            {{0,1},{1,1},{2,1},{0,2}}, // J
            {{0,1},{1,1},{2,1},{2,2}}, // L
        };
        for (int p = 0; p < NUM_PIECES; ++p) {
            for (int i = 0; i < 4; ++i) cells[p][0][i] = spawn_cells[p][i];
            int pivot2 = (p == P_I || p == P_O) ? 3 : 2;
            for (int r = 1; r < 4; ++r) {
                for (int i = 0; i < 4; ++i) {
                    if (p == P_O) {
                        cells[p][r][i] = spawn_cells[p][i];
                    } else {
                        auto prev = cells[p][r - 1][i];
                        cells[p][r][i] = {prev.dy, static_cast<int8_t>(pivot2 - prev.dx)};
                    }
                }
            }
        }
        // Standard SRS 90-degree kicks
        auto set_k = [](KickList& kl, std::initializer_list<Offset> list) {
            kl.count = 0;
            for (auto o : list) if (kl.count < 8) kl.data[kl.count++] = o;
        };
        for (int p = 0; p < NUM_PIECES; ++p) {
            if (p == P_O) {
                for (int r = 0; r < 4; ++r) {
                    set_k(kicks90[p][r][0], {{0,0}});
                    set_k(kicks90[p][r][1], {{0,0}});
                }
            } else if (p == P_I) {
                set_k(kicks90[p][0][0], {{0,0},{-2,0},{1,0},{-2,-1},{1,2}});
                set_k(kicks90[p][1][1], {{0,0},{2,0},{-1,0},{2,1},{-1,-2}});
                set_k(kicks90[p][1][0], {{0,0},{-1,0},{2,0},{-1,2},{2,-1}});
                set_k(kicks90[p][2][1], {{0,0},{1,0},{-2,0},{1,-2},{-2,1}});
                set_k(kicks90[p][2][0], {{0,0},{2,0},{-1,0},{2,1},{-1,-2}});
                set_k(kicks90[p][3][1], {{0,0},{-2,0},{1,0},{-2,-1},{1,2}});
                set_k(kicks90[p][3][0], {{0,0},{1,0},{-2,0},{1,-2},{-2,1}});
                set_k(kicks90[p][0][1], {{0,0},{-1,0},{2,0},{-1,2},{2,-1}});
            } else {
                set_k(kicks90[p][0][0], {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}});
                set_k(kicks90[p][1][1], {{0,0},{1,0},{1,-1},{0,2},{1,2}});
                set_k(kicks90[p][1][0], {{0,0},{1,0},{1,-1},{0,2},{1,2}});
                set_k(kicks90[p][2][1], {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}});
                set_k(kicks90[p][2][0], {{0,0},{1,0},{1,1},{0,-2},{1,-2}});
                set_k(kicks90[p][3][1], {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}});
                set_k(kicks90[p][3][0], {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}});
                set_k(kicks90[p][0][1], {{0,0},{1,0},{1,1},{0,-2},{1,-2}});
            }
        }
        // v4/v5 directional SRS+ 180-degree kicks
        set_k(kicks180[0], {{0,0},{0,1},{1,1},{-1,1},{1,0},{-1,0}});
        set_k(kicks180[2], {{0,0},{0,-1},{-1,-1},{1,-1},{-1,0},{1,0}});
        set_k(kicks180[1], {{0,0},{1,0},{1,2},{1,1},{0,2},{0,1}});
        set_k(kicks180[3], {{0,0},{-1,0},{-1,2},{-1,1},{0,2},{0,1}});
        recompute_derived();
    }

    void recompute_derived() {
        for (int p = 0; p < NUM_PIECES; ++p) {
            for (int r = 0; r < 4; ++r) {
                int8_t x0 = 127, x1 = -127, y0 = 127, y1 = -127;
                for (int i = 0; i < 4; ++i) {
                    x0 = std::min(x0, cells[p][r][i].dx);
                    x1 = std::max(x1, cells[p][r][i].dx);
                    y0 = std::min(y0, cells[p][r][i].dy);
                    y1 = std::max(y1, cells[p][r][i].dy);
                }
                min_dx[p][r] = x0; max_dx[p][r] = x1;
                min_dy[p][r] = y0; max_dy[p][r] = y1;
            }
            // Check if all 4 rotations occupy identical cell sets
            bool same = true;
            auto norm = [&](int r) {
                std::array<int, 4> a;
                for (int i = 0; i < 4; ++i) a[i] = cells[p][r][i].dy * 8 + cells[p][r][i].dx;
                std::sort(a.begin(), a.end());
                return a;
            };
            auto r0 = norm(0);
            for (int r = 1; r < 4; ++r) if (norm(r) != r0) same = false;
            unique_rot1[p] = same;

            SpawnMask sm;
            uint16_t row_bits[BOARD_H] = {};
            for (int i = 0; i < 4; ++i) {
                int cx = spawn_x[p] + cells[p][0][i].dx;
                int cy = spawn_y[p] + cells[p][0][i].dy;
                if (cx < 0 || cx >= BOARD_W || cy < 0 || cy >= BOARD_H) {
                    sm.valid = false;
                    break;
                }
                row_bits[cy] |= static_cast<uint16_t>(1u << cx);
            }
            if (sm.valid) {
                for (int y = 0; y < BOARD_H; ++y) {
                    if (row_bits[y]) sm.rows[sm.count++] = {static_cast<uint8_t>(y), row_bits[y]};
                }
            }
            spawn_masks[p] = sm;
        }
    }

    void load_from_json(const Json* rules, const Json* geom) {
        if (rules && rules->type == Json::OBJ) {
            if (auto* v = rules->get("overtime_first_frame")) {
                is_realtime = true;
                overtime_first_frame = v->as_int(3600);
            } else {
                is_realtime = false;
            }
            if (auto* v = rules->get("profile"); v && v->type == Json::STR) {
                has_delayed_locks = is_realtime && (v->sval != "reference-realtime-v5");
            } else {
                has_delayed_locks = is_realtime;
            }
            if (auto* v = rules->get("normal_attack"); v && v->type == Json::ARR) {
                for (size_t i = 0; i < 5 && i < v->arr.size(); ++i) normal_attack[i] = v->arr[i].as_int();
            }
            if (auto* v = rules->get("tspin_attack"); v && v->type == Json::ARR) {
                for (size_t i = 0; i < 4 && i < v->arr.size(); ++i) tspin_attack[i] = v->arr[i].as_int();
            }
            if (auto* v = rules->get("b2b_bonus")) b2b_bonus = v->as_int(1);
            if (auto* v = rules->get("perfect_clear_floor")) perfect_clear_floor = v->as_int(10);
            if (auto* v = rules->get("max_commands")) max_commands = v->as_int(512);
            if (auto* v = rules->get("garbage_cap")) garbage_cap = (v->type == Json::NUM) ? v->as_int(8) : -1;
            else garbage_cap = -1;
            if (auto* v = rules->get("move_frames")) move_frames = v->as_int(2);
            if (auto* v = rules->get("rotate_frames")) rotate_frames = v->as_int(1);
            if (auto* v = rules->get("soft_drop_frames")) soft_drop_frames = v->as_int(2);
            if (auto* v = rules->get("hold_frames")) hold_frames = v->as_int(2);
            if (auto* v = rules->get("lock_frames")) lock_frames = v->as_int(8);
            if (auto* v = rules->get("clear_frames")) clear_frames = v->as_int(12);
            if (auto* v = rules->get("special_clear_frames")) special_clear_frames = v->as_int(16);
            if (auto* v = rules->get("free_think_ms")) free_think_ms = v->as_int(30);
            if (auto* v = rules->get("garbage_delay_frames")) garbage_delay_frames = v->as_int(30);
            if (auto* v = rules->get("combo_block_limit")) combo_block_limit = v->as_int(3);
            if (auto* v = rules->get("overtime_interval_frames")) overtime_interval_frames = v->as_int(240);
            if (auto* v = rules->get("max_frames")) max_frames = v->as_int(10800);
            if (auto* v = rules->get("overtime_first")) overtime_first_turn = v->as_int(150);
            if (auto* v = rules->get("overtime_interval")) overtime_interval_turn = v->as_int(8);
            if (auto* so = rules->get("spawn_origins"); so && so->type == Json::OBJ) {
                for (const auto& kv : so->obj) {
                    if (kv.first.size() == 1 && kv.second.type == Json::ARR && kv.second.arr.size() == 2) {
                        int8_t p = char_to_piece(kv.first[0]);
                        if (p >= 0) {
                            spawn_x[p] = static_cast<int8_t>(kv.second.arr[0].as_int());
                            spawn_y[p] = static_cast<int8_t>(kv.second.arr[1].as_int());
                        }
                    }
                }
            }
        }
        if (geom && geom->type == Json::OBJ) {
            auto load_kicks = [](const Json& arr, KickList& kl) {
                kl.count = 0;
                if (arr.type != Json::ARR) return;
                for (const auto& pair : arr.arr) {
                    if (pair.type == Json::ARR && pair.arr.size() == 2 && kl.count < 8) {
                        kl.data[kl.count++] = {
                            static_cast<int8_t>(pair.arr[0].as_int()),
                            static_cast<int8_t>(pair.arr[1].as_int())
                        };
                    }
                }
            };
            if (auto* cobj = geom->get("cells"); cobj && cobj->type == Json::OBJ) {
                for (const auto& kv : cobj->obj) {
                    if (kv.first.size() != 1 || kv.second.type != Json::ARR) continue;
                    int8_t p = char_to_piece(kv.first[0]);
                    if (p < 0) continue;
                    for (int r = 0; r < 4 && static_cast<size_t>(r) < kv.second.arr.size(); ++r) {
                        const auto& rarr = kv.second.arr[r];
                        for (int i = 0; i < 4 && static_cast<size_t>(i) < rarr.arr.size(); ++i) {
                            cells[p][r][i] = {
                                static_cast<int8_t>(rarr.arr[i].arr[0].as_int()),
                                static_cast<int8_t>(rarr.arr[i].arr[1].as_int())
                            };
                        }
                    }
                }
            }
            if (auto* k90 = geom->get("kicks_90"); k90 && k90->type == Json::OBJ) {
                for (const auto& kv : k90->obj) {
                    if (kv.first.size() != 1 || kv.second.type != Json::OBJ) continue;
                    int8_t p = char_to_piece(kv.first[0]);
                    if (p < 0) continue;
                    for (int r = 0; r < 4; ++r) {
                        std::string cw = std::to_string(r) + ":" + std::to_string((r + 1) & 3);
                        std::string ccw = std::to_string(r) + ":" + std::to_string((r + 3) & 3);
                        if (auto* a = kv.second.get(cw)) load_kicks(*a, kicks90[p][r][0]);
                        if (auto* a = kv.second.get(ccw)) load_kicks(*a, kicks90[p][r][1]);
                    }
                }
            }
            if (auto* k180 = geom->get("kicks_180")) {
                if (k180->type == Json::OBJ) {
                    for (int r = 0; r < 4; ++r) {
                        std::string k = std::to_string(r) + ":" + std::to_string((r + 2) & 3);
                        if (auto* a = k180->get(k)) load_kicks(*a, kicks180[r]);
                    }
                } else if (k180->type == Json::ARR) {
                    for (int r = 0; r < 4; ++r) load_kicks(*k180, kicks180[r]);
                }
            }
        }
        recompute_derived();
    }

    inline int post_lock_frames(int lines, uint8_t spin) const {
        if (!is_realtime) return 1;
        if (lines == 0) return lock_frames;
        return lock_frames + ((lines == 4 || spin == SPIN_FULL) ? special_clear_frames : clear_frames);
    }

    inline int overtime_count(int frame) const {
        if (!is_realtime || frame < overtime_first_frame) return 0;
        return 1 + (frame - overtime_first_frame) / overtime_interval_frames;
    }
};

// ------------------------- Board & Bitboard Physics -------------------------

struct BoardState {
    std::array<uint16_t, BOARD_H> rows{};
    uint32_t solid_mask = 0;   // overtime solid rows (cell == 3)
    uint32_t garbage_mask = 0; // known garbage rows (cell == 2)
    uint32_t opaque_mask = 0;  // predicted future garbage rows (unknown hole, unclearable)

    inline bool fits(const RulesConfig& cfg, int8_t p, int x, int y, int r) const {
        if (x + cfg.min_dx[p][r] < 0 || x + cfg.max_dx[p][r] >= BOARD_W) return false;
        if (y + cfg.min_dy[p][r] < 0 || y + cfg.max_dy[p][r] >= BOARD_H) return false;
        const auto& c = cfg.cells[p][r];
        return !((rows[y + c[0].dy] & (1u << (x + c[0].dx))) ||
                 (rows[y + c[1].dy] & (1u << (x + c[1].dx))) ||
                 (rows[y + c[2].dy] & (1u << (x + c[2].dx))) ||
                 (rows[y + c[3].dy] & (1u << (x + c[3].dx))));
    }

    inline uint8_t spawnable_mask(const RulesConfig& cfg) const {
        uint8_t mask = 0;
        for (int p = 0; p < NUM_PIECES; ++p) {
            const auto& sm = cfg.spawn_masks[p];
            if (!sm.valid) continue;
            bool ok = true;
            for (uint8_t i = 0; i < sm.count; ++i) {
                if (rows[sm.rows[i].first] & sm.rows[i].second) { ok = false; break; }
            }
            if (ok) mask |= static_cast<uint8_t>(1u << p);
        }
        return mask;
    }

    inline int solid_floor() const {
        int f = 0;
        while (f < BOARD_H && ((solid_mask >> f) & 1u) && rows[f] == FULL_ROW) ++f;
        return f;
    }

    inline uint64_t hash64() const {
        uint64_t h = 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(solid_mask) << 32) ^ opaque_mask;
        for (int y = 0; y < BOARD_H; ++y) {
            if (rows[y]) {
                uint64_t v = (static_cast<uint64_t>(y) << 16) | rows[y];
                h ^= v * 0xBF58476D1CE4E5B9ULL;
                h = (h << 13) | (h >> 51);
            }
        }
        return h;
    }
};

struct WarningItem {
    int16_t lines = 0;
    int32_t due = 0;
};

struct WarningQueue {
    std::array<WarningItem, 8> items{};
    uint8_t count = 0;

    int total_lines() const {
        int s = 0;
        for (uint8_t i = 0; i < count; ++i) s += items[i].lines;
        return s;
    }
    int due_by(int tick) const {
        int s = 0;
        for (uint8_t i = 0; i < count; ++i) if (items[i].due <= tick) s += items[i].lines;
        return s;
    }
};

struct Placement {
    BoardState after;
    uint8_t lines = 0;
    uint8_t spin = SPIN_NONE;
    bool perfect_clear = false;
    uint16_t pre_lock_frames = 0;
    uint16_t cmd_len = 0;
    uint32_t packed_cells = 0;
    // Only populated for root candidates
    std::vector<uint8_t> cmds;
};

struct SettlementResult {
    BoardState board;
    WarningQueue warnings;
    int attack = 0;
    int sent = 0;
    int cancelled = 0;
    int risen = 0;
    int combo = 0;
    bool b2b = false;
    int combo_blocks = 0;
    bool combo_blocked = false;
    int next_frame = 0;
    int total_frames = 0; // pre + post
    bool alive = true;
    uint8_t spawn_mask = 0x7F;
};

inline int combo_bonus(int combo) {
    if (combo <= 2) return 0;
    if (combo <= 5) return 1;
    if (combo <= 7) return 2;
    if (combo <= 9) return 3;
    if (combo <= 12) return 4;
    return 5;
}

inline void score_clear(const RulesConfig& cfg, int lines, uint8_t spin, bool pc,
                        int in_combo, bool in_b2b, int& out_attack, int& out_combo, bool& out_b2b) {
    if (lines == 0) {
        out_attack = 0;
        out_combo = 0;
        out_b2b = in_b2b;
        return;
    }
    out_combo = in_combo + 1;
    bool t_attack = (spin == SPIN_FULL || (spin == SPIN_MINI && lines >= 2));
    bool difficult = (lines == 4 || t_attack);
    int atk = t_attack ? cfg.tspin_attack[lines] : cfg.normal_attack[lines];
    out_b2b = in_b2b;
    if (difficult) {
        if (in_b2b) atk += cfg.b2b_bonus;
        out_b2b = true;
    } else if (spin != SPIN_MINI) {
        out_b2b = false;
    }
    atk += combo_bonus(out_combo);
    if (pc && atk < cfg.perfect_clear_floor) atk = cfg.perfect_clear_floor;
    out_attack = atk;
}

inline SettlementResult apply_settlement(
    const RulesConfig& cfg,
    const Placement& pl,
    const WarningQueue& in_warnings,
    int in_combo,
    bool in_b2b,
    int in_combo_blocks,
    int cur_tick,
    bool hold_used,
    int8_t next_piece) {

    SettlementResult res;
    res.board = pl.after;
    int pre_frames = cfg.is_realtime ? (pl.pre_lock_frames + (hold_used ? cfg.hold_frames : 0)) : 0;
    int lock_tick = cur_tick + pre_frames;

    // 1. Score attack
    score_clear(cfg, pl.lines, pl.spin, pl.perfect_clear,
                in_combo, in_b2b, res.attack, res.combo, res.b2b);

    // 2. FIFO cancel old warnings
    int rem_atk = res.attack;
    WarningQueue due_q, fut_q;
    for (uint8_t i = 0; i < in_warnings.count; ++i) {
        int lines = in_warnings.items[i].lines;
        int due = in_warnings.items[i].due;
        int used = std::min(rem_atk, lines);
        rem_atk -= used;
        res.cancelled += used;
        int left = lines - used;
        if (left > 0) {
            WarningItem wi{static_cast<int16_t>(left), due};
            if (due <= lock_tick) {
                if (due_q.count < 8) due_q.items[due_q.count++] = wi;
            } else {
                if (fut_q.count < 8) fut_q.items[fut_q.count++] = wi;
            }
        }
    }
    res.sent = rem_atk;

    bool overflow = false;
    res.combo_blocks = in_combo_blocks;

    // 3. Limited Combo Blocking & Garbage Rise
    if (cfg.is_realtime && due_q.count > 0 && pl.lines > 0 && in_combo_blocks < cfg.combo_block_limit) {
        res.combo_blocks = in_combo_blocks + 1;
        res.combo_blocked = true;
        for (uint8_t i = 0; i < due_q.count && res.warnings.count < 8; ++i) {
            res.warnings.items[res.warnings.count++] = {due_q.items[i].lines, lock_tick};
        }
        for (uint8_t i = 0; i < fut_q.count && res.warnings.count < 8; ++i) {
            res.warnings.items[res.warnings.count++] = fut_q.items[i];
        }
    } else {
        int budget = (cfg.garbage_cap >= 0) ? cfg.garbage_cap : 1000;
        WarningQueue def_q;
        for (uint8_t i = 0; i < due_q.count; ++i) {
            int count = due_q.items[i].lines;
            int take = std::min(count, budget);
            budget -= take;
            if (take > 0) {
                res.risen += take;
                int floor = res.board.solid_floor();
                int capacity = BOARD_H - floor;
                int retained = std::max(0, capacity - take);
                if (take > capacity) overflow = true;
                for (int y = floor + retained; y < BOARD_H; ++y) {
                    if (res.board.rows[y]) { overflow = true; break; }
                }
                int inserted = std::min(take, capacity);
                std::array<uint16_t, BOARD_H> nr{};
                uint32_t nsolid = 0, ngarb = 0, nopaque = 0;
                for (int y = 0; y < floor; ++y) {
                    nr[y] = res.board.rows[y];
                    if ((res.board.solid_mask >> y) & 1u) nsolid |= (1u << y);
                }
                for (int y = floor; y < floor + inserted; ++y) {
                    nr[y] = FULL_ROW;
                    ngarb |= (1u << y);
                    nopaque |= (1u << y);
                }
                for (int y = 0; y < retained; ++y) {
                    int src = floor + y;
                    int dst = floor + inserted + y;
                    nr[dst] = res.board.rows[src];
                    if ((res.board.solid_mask >> src) & 1u) nsolid |= (1u << dst);
                    if ((res.board.garbage_mask >> src) & 1u) ngarb |= (1u << dst);
                    if ((res.board.opaque_mask >> src) & 1u) nopaque |= (1u << dst);
                }
                res.board.rows = nr;
                res.board.solid_mask = nsolid;
                res.board.garbage_mask = ngarb;
                res.board.opaque_mask = nopaque;
            }
            if (count > take && def_q.count < 8) {
                def_q.items[def_q.count++] = {static_cast<int16_t>(count - take), lock_tick + 1};
            }
        }
        for (uint8_t i = 0; i < def_q.count && res.warnings.count < 8; ++i)
            res.warnings.items[res.warnings.count++] = def_q.items[i];
        for (uint8_t i = 0; i < fut_q.count && res.warnings.count < 8; ++i)
            res.warnings.items[res.warnings.count++] = fut_q.items[i];
        if (pl.lines == 0) res.combo_blocks = 0;
    }

    // 4. Overtime solid rows
    int post_frames = cfg.post_lock_frames(pl.lines, pl.spin);
    res.total_frames = pre_frames + post_frames;
    res.next_frame = lock_tick + post_frames;

    int solid_delta = 0;
    if (cfg.is_realtime) {
        solid_delta = std::max(0, cfg.overtime_count(res.next_frame) - cfg.overtime_count(cur_tick));
    } else if (cur_tick >= cfg.overtime_first_turn &&
               (cur_tick - cfg.overtime_first_turn) % cfg.overtime_interval_turn == 0) {
        solid_delta = 1;
    }
    if (solid_delta > 0) {
        for (int s = 0; s < solid_delta; ++s) {
            if (res.board.rows[BOARD_H - 1]) overflow = true;
            for (int y = BOARD_H - 1; y > 0; --y) res.board.rows[y] = res.board.rows[y - 1];
            res.board.rows[0] = FULL_ROW;
            res.board.solid_mask = ((res.board.solid_mask << 1) | 1u) & ((1u << BOARD_H) - 1u);
            res.board.garbage_mask = (res.board.garbage_mask << 1) & ((1u << BOARD_H) - 1u);
            res.board.opaque_mask = (res.board.opaque_mask << 1) & ((1u << BOARD_H) - 1u);
        }
    }

    // 5. Spawn check
    res.spawn_mask = res.board.spawnable_mask(cfg);
    bool next_ok = (next_piece >= 0) ? (((res.spawn_mask >> next_piece) & 1u) != 0) : (res.spawn_mask != 0);
    res.alive = (!overflow) && next_ok;
    return res;
}

// ------------------------- Move Generation (Finesse Dijkstra + Fast) -------------------------

inline uint32_t pack_sorted_cells(const RulesConfig& cfg, int8_t p, int x, int y, int r) {
    const auto& c = cfg.cells[p][r];
    uint32_t a = static_cast<uint32_t>((y + c[0].dy) * 10 + (x + c[0].dx));
    uint32_t b = static_cast<uint32_t>((y + c[1].dy) * 10 + (x + c[1].dx));
    uint32_t d = static_cast<uint32_t>((y + c[2].dy) * 10 + (x + c[2].dx));
    uint32_t e = static_cast<uint32_t>((y + c[3].dy) * 10 + (x + c[3].dx));
    if (a > b) std::swap(a, b);
    if (d > e) std::swap(d, e);
    if (a > d) std::swap(a, d);
    if (b > e) std::swap(b, e);
    if (b > d) std::swap(b, d);
    return a | (b << 8) | (d << 16) | (e << 24);
}

inline void finalize_placement(
    const RulesConfig& cfg,
    const BoardState& board,
    int8_t piece,
    int x, int y, int r, uint8_t spin_flag,
    uint16_t pre_frames, uint16_t cmd_len, uint32_t packed_cells,
    Placement& out) {

    std::array<uint16_t, BOARD_H> packed = board.rows;
    const auto& c = cfg.cells[piece][r];
    for (int i = 0; i < 4; ++i) {
        packed[y + c[i].dy] |= static_cast<uint16_t>(1u << (x + c[i].dx));
    }

    uint32_t clear_mask = 0;
    uint8_t cleared = 0;
    uint32_t blocked_clear = board.solid_mask | board.opaque_mask;
    for (int row = 0; row < BOARD_H; ++row) {
        if (packed[row] == FULL_ROW && !((blocked_clear >> row) & 1u)) {
            clear_mask |= (1u << row);
            ++cleared;
        }
    }

    uint8_t spin = SPIN_NONE;
    if (piece == P_T && spin_flag != 0) {
        auto corner = [&](int cx, int cy) -> bool {
            if (cx < 0 || cx >= BOARD_W || cy < 0) return true;
            if (cy >= BOARD_H) return false;
            if ((board.opaque_mask >> cy) & 1u) return false;
            return (packed[cy] & (1u << cx)) != 0;
        };
        bool c0 = corner(x, y + 2);
        bool c1 = corner(x + 2, y + 2);
        bool c2 = corner(x + 2, y);
        bool c3 = corner(x, y);
        int cnt = static_cast<int>(c0) + c1 + c2 + c3;
        if (cnt >= 3) {
            bool cs[4] = {c0, c1, c2, c3};
            if ((cs[r] && cs[(r + 1) & 3]) || cleared >= 2 || spin_flag == 2) {
                spin = SPIN_FULL;
            } else {
                spin = SPIN_MINI;
            }
        }
    }

    BoardState next_b;
    int dst = 0;
    bool any_nonempty = false;
    for (int row = 0; row < BOARD_H; ++row) {
        if ((clear_mask >> row) & 1u) continue;
        next_b.rows[dst] = packed[row];
        if (packed[row]) any_nonempty = true;
        if ((board.solid_mask >> row) & 1u) next_b.solid_mask |= (1u << dst);
        if ((board.garbage_mask >> row) & 1u) next_b.garbage_mask |= (1u << dst);
        if ((board.opaque_mask >> row) & 1u) next_b.opaque_mask |= (1u << dst);
        ++dst;
    }

    out.after = next_b;
    out.lines = cleared;
    out.spin = spin;
    out.perfect_clear = !any_nonempty;
    out.pre_lock_frames = pre_frames;
    out.cmd_len = cmd_len;
    out.packed_cells = packed_cells;
}

// Reusable scratch buffers for zero-allocation Finesse Dijkstra movegen
struct MovegenScratch {
    static constexpr int X_OFF = 3;
    static constexpr int Y_OFF = 3;
    static constexpr int NX = 14;
    static constexpr int NY = 28;
    static constexpr int NSTATES = NX * NY * 4 * 3; // 4704
    static constexpr int BUCKETS = 64;

    std::array<uint16_t, NSTATES> dist{};
    std::array<uint16_t, NSTATES> steps{};
    std::array<int16_t, NSTATES> parent{};
    std::array<uint8_t, NSTATES> parent_cmd{};
    std::array<int8_t, NX * NY * 4> fall_y{};
    std::array<std::vector<uint16_t>, BUCKETS> buckets;
    std::vector<uint64_t> seen_landings;

    static inline int encode(int x, int y, int r, int f) {
        return (((x + X_OFF) * NY + (y + Y_OFF)) * 4 + r) * 3 + f;
    }
    static inline int encode_xyr(int x, int y, int r) {
        return ((x + X_OFF) * NY + (y + Y_OFF)) * 4 + r;
    }
};

// Full reachable Finesse Dijkstra movegen: guarantees minimum pre_lock_frames for every reachable landing!
void generate_moves_full(
    const RulesConfig& cfg,
    const BoardState& board,
    int8_t piece,
    bool record_cmds,
    MovegenScratch& sc,
    std::vector<Placement>& out) {

    out.clear();
    int sx = cfg.spawn_x[piece];
    int sy = cfg.spawn_y[piece];
    if (!board.fits(cfg, piece, sx, sy, 0)) return;

    sc.dist.fill(0xFFFFu);
    sc.fall_y.fill(-120);
    for (auto& b : sc.buckets) b.clear();
    sc.seen_landings.clear();

    auto get_fall_y = [&](int x, int y, int r) -> int {
        int idx = MovegenScratch::encode_xyr(x, y, r);
        int8_t cached = sc.fall_y[idx];
        if (cached != -120) return cached;
        int ty = y;
        while (board.fits(cfg, piece, x, ty - 1, r)) --ty;
        for (int yy = y; yy >= ty; --yy) {
            sc.fall_y[MovegenScratch::encode_xyr(x, yy, r)] = static_cast<int8_t>(ty);
        }
        return ty;
    };

    int start_id = MovegenScratch::encode(sx, sy, 0, 0);
    sc.dist[start_id] = 0;
    sc.steps[start_id] = 0;
    sc.parent[start_id] = -1;
    sc.buckets[0].push_back(static_cast<uint16_t>(start_id));

    const int max_cmds_before_drop = std::max(0, cfg.max_commands - 1);
    const bool is_t = (piece == P_T);
    const bool only_one_rot = cfg.unique_rot1[piece];

    // Fixed command list with frame costs
    const int c_rot = std::max(1, cfg.is_realtime ? cfg.rotate_frames : 1);
    const int c_mov = std::max(1, cfg.is_realtime ? cfg.move_frames : 1);
    const int c_dn  = std::max(1, cfg.is_realtime ? cfg.soft_drop_frames : 1);

    int active_count = 1;
    uint16_t cur_cost = 0;

    auto try_relax = [&](int nx, int ny, int nr, int nf, uint16_t ncost, uint16_t nsteps, int pid, uint8_t cmd) {
        if (nsteps > max_cmds_before_drop) return;
        int nid = MovegenScratch::encode(nx, ny, nr, nf);
        if (ncost < sc.dist[nid] || (ncost == sc.dist[nid] && nsteps < sc.steps[nid])) {
            bool was_unseen = (sc.dist[nid] == 0xFFFFu);
            sc.dist[nid] = ncost;
            sc.steps[nid] = nsteps;
            sc.parent[nid] = static_cast<int16_t>(pid);
            sc.parent_cmd[nid] = cmd;
            sc.buckets[ncost & (MovegenScratch::BUCKETS - 1)].push_back(static_cast<uint16_t>(nid));
            if (was_unseen) ++active_count;
            else ++active_count;
        }
    };

    auto has_landing = [&](uint64_t key) -> bool {
        for (uint64_t k : sc.seen_landings) if (k == key) return true;
        return false;
    };

    while (active_count > 0) {
        auto& bucket = sc.buckets[cur_cost & (MovegenScratch::BUCKETS - 1)];
        while (!bucket.empty()) {
            uint16_t id = bucket.back();
            bucket.pop_back();
            --active_count;
            if (sc.dist[id] != cur_cost) continue;

            // Decode state
            int tmp = id;
            int flag = tmp % 3; tmp /= 3;
            int r = tmp & 3; tmp >>= 2;
            int y = (tmp % MovegenScratch::NY) - MovegenScratch::Y_OFF;
            int x = (tmp / MovegenScratch::NY) - MovegenScratch::X_OFF;
            uint16_t st = sc.steps[id];

            // Try hard-dropping from this state
            int fy = get_fall_y(x, y, r);
            uint8_t final_flag = (fy == y) ? static_cast<uint8_t>(flag) : 0;
            uint32_t pcells = pack_sorted_cells(cfg, piece, x, fy, r);

            // Compute spin quickly to form exact dedup key
            uint8_t spin_preview = SPIN_NONE;
            if (is_t && final_flag != 0) {
                std::array<uint16_t, BOARD_H> packed = board.rows;
                const auto& c = cfg.cells[P_T][r];
                for (int i = 0; i < 4; ++i) packed[fy + c[i].dy] |= static_cast<uint16_t>(1u << (x + c[i].dx));
                uint8_t clr = 0;
                uint32_t blk = board.solid_mask | board.opaque_mask;
                for (int row = 0; row < BOARD_H; ++row) {
                    if (packed[row] == FULL_ROW && !((blk >> row) & 1u)) ++clr;
                }
                auto corner = [&](int cx, int cy) -> bool {
                    if (cx < 0 || cx >= BOARD_W || cy < 0) return true;
                    if (cy >= BOARD_H) return false;
                    if ((board.opaque_mask >> cy) & 1u) return false;
                    return (packed[cy] & (1u << cx)) != 0;
                };
                bool cs[4] = {corner(x, fy + 2), corner(x + 2, fy + 2), corner(x + 2, fy), corner(x, fy)};
                int cnt = static_cast<int>(cs[0]) + cs[1] + cs[2] + cs[3];
                if (cnt >= 3) {
                    spin_preview = ((cs[r] && cs[(r + 1) & 3]) || clr >= 2 || final_flag == 2) ? SPIN_FULL : SPIN_MINI;
                }
            }
            uint64_t lkey = (static_cast<uint64_t>(pcells) << 2) | spin_preview;
            if (!has_landing(lkey)) {
                sc.seen_landings.push_back(lkey);
                Placement pl;
                finalize_placement(cfg, board, piece, x, fy, r, final_flag, cur_cost, st + 1, pcells, pl);
                if (record_cmds) {
                    pl.cmds.reserve(st + 1);
                    int cur = id;
                    while (sc.parent[cur] != -1) {
                        pl.cmds.push_back(sc.parent_cmd[cur]);
                        cur = sc.parent[cur];
                    }
                    std::reverse(pl.cmds.begin(), pl.cmds.end());
                    pl.cmds.push_back(CMD_HARD_DROP);
                }
                out.push_back(std::move(pl));
            }

            if (st >= max_cmds_before_drop) continue;

            // Expand moves
            if (board.fits(cfg, piece, x - 1, y, r))
                try_relax(x - 1, y, r, 0, cur_cost + c_mov, st + 1, id, CMD_LEFT);
            if (board.fits(cfg, piece, x + 1, y, r))
                try_relax(x + 1, y, r, 0, cur_cost + c_mov, st + 1, id, CMD_RIGHT);
            if (board.fits(cfg, piece, x, y - 1, r))
                try_relax(x, y - 1, r, 0, cur_cost + c_dn, st + 1, id, CMD_DOWN);

            if (!only_one_rot) {
                // CW
                {
                    int nr = (r + 1) & 3;
                    const auto& kl = cfg.kicks90[piece][r][0];
                    for (uint8_t k = 0; k < kl.count; ++k) {
                        int nx = x + kl.data[k].dx, ny = y + kl.data[k].dy;
                        if (board.fits(cfg, piece, nx, ny, nr)) {
                            int nf = is_t ? (k == 4 ? 2 : 1) : 0;
                            try_relax(nx, ny, nr, nf, cur_cost + c_rot, st + 1, id, CMD_CW);
                            break;
                        }
                    }
                }
                // CCW
                {
                    int nr = (r + 3) & 3;
                    const auto& kl = cfg.kicks90[piece][r][1];
                    for (uint8_t k = 0; k < kl.count; ++k) {
                        int nx = x + kl.data[k].dx, ny = y + kl.data[k].dy;
                        if (board.fits(cfg, piece, nx, ny, nr)) {
                            int nf = is_t ? (k == 4 ? 2 : 1) : 0;
                            try_relax(nx, ny, nr, nf, cur_cost + c_rot, st + 1, id, CMD_CCW);
                            break;
                        }
                    }
                }
                // ROT180
                {
                    int nr = (r + 2) & 3;
                    const auto& kl = cfg.kicks180[r];
                    for (uint8_t k = 0; k < kl.count; ++k) {
                        int nx = x + kl.data[k].dx, ny = y + kl.data[k].dy;
                        if (board.fits(cfg, piece, nx, ny, nr)) {
                            int nf = is_t ? 1 : 0;
                            try_relax(nx, ny, nr, nf, cur_cost + c_rot, st + 1, id, CMD_ROT180);
                            break;
                        }
                    }
                }
            }
        }
        ++cur_cost;
    }
}

// Ultra-fast hard-drop-only movegen for inner search plies (exact frame cost, ~2 us!)
void generate_moves_fast(
    const RulesConfig& cfg,
    const BoardState& board,
    int8_t piece,
    std::vector<Placement>& out) {

    out.clear();
    int sx = cfg.spawn_x[piece];
    int sy = cfg.spawn_y[piece];
    if (!board.fits(cfg, piece, sx, sy, 0)) return;

    const int c_rot = cfg.is_realtime ? cfg.rotate_frames : 1;
    const int c_mov = cfg.is_realtime ? cfg.move_frames : 1;
    const int rot_count = cfg.unique_rot1[piece] ? 1 : 4;

    uint32_t seen[48];
    int seen_n = 0;

    // Rotation order: 0 (0 frames), 1 CW (1), 3 CCW (1), 2 ROT180 (1)
    constexpr int rots[4] = {0, 1, 3, 2};
    for (int ri = 0; ri < rot_count; ++ri) {
        int r = rots[ri];
        int rx = sx, ry = sy;
        uint16_t base_cost = 0;
        uint16_t base_len = 0;
        if (r != 0) {
            base_cost = static_cast<uint16_t>(c_rot);
            base_len = 1;
            const KickList& kl = (r == 1) ? cfg.kicks90[piece][0][0]
                               : (r == 3) ? cfg.kicks90[piece][0][1]
                                          : cfg.kicks180[0];
            bool ok = false;
            for (uint8_t k = 0; k < kl.count; ++k) {
                if (board.fits(cfg, piece, sx + kl.data[k].dx, sy + kl.data[k].dy, r)) {
                    rx = sx + kl.data[k].dx;
                    ry = sy + kl.data[k].dy;
                    ok = true;
                    break;
                }
            }
            if (!ok) continue;
        }

        auto emit_pos = [&](int x, uint16_t cost, uint16_t len) {
            int fy = ry;
            while (board.fits(cfg, piece, x, fy - 1, r)) --fy;
            uint32_t pcells = pack_sorted_cells(cfg, piece, x, fy, r);
            for (int i = 0; i < seen_n; ++i) {
                if (seen[i] == pcells) return;
            }
            if (seen_n < 48) seen[seen_n++] = pcells;
            Placement pl;
            finalize_placement(cfg, board, piece, x, fy, r, 0, cost, len + 1, pcells, pl);
            out.push_back(std::move(pl));
        };

        // Shift 0, then left, then right (expanding outward guarantees min cost first)
        emit_pos(rx, base_cost, base_len);
        for (int s = 1; s <= 6; ++s) {
            if (!board.fits(cfg, piece, rx - s, ry, r)) break;
            emit_pos(rx - s, static_cast<uint16_t>(base_cost + s * c_mov), static_cast<uint16_t>(base_len + s));
        }
        for (int s = 1; s <= 6; ++s) {
            if (!board.fits(cfg, piece, rx + s, ry, r)) break;
            emit_pos(rx + s, static_cast<uint16_t>(base_cost + s * c_mov), static_cast<uint16_t>(base_len + s));
        }
    }
}

// ------------------------- Board Evaluation -------------------------

struct EvalContext {
    int opp_max_h = 0;
    int opp_holes = 0;
    bool has_t_soon = false; // T in hold or next 3 pieces
    bool has_i_soon = false; // I in hold or next 3 pieces
    int incoming_total = 0;
};

struct BoardFeatures {
    int base = 0;
    std::array<int, BOARD_W> h{};
    std::array<int, BOARD_W> rel{};
    int max_h = 0;
    int max_rel = 0;
    int sum_rel = 0;
    int holes = 0;
    int covered = 0;
    int row_trans = 0;
    int col_trans = 0;
    int well_col = 9;
    int well_depth = 0;
    int extra_wells = 0;
    int bumpiness = 0;
    int bump_sq = 0;
    int center_peak = 0;
    int garbage_rows = 0;
    int garbage_cover = 0; // blocks sitting on top of topmost known garbage hole
    int tsd_slots = 0;
    int tsd_bases = 0;
};

inline BoardFeatures extract_features(const BoardState& b, bool detect_tsd) {
    BoardFeatures f;
    f.base = b.solid_floor();
    uint32_t floor_mask = (f.base >= 32) ? 0xFFFFFFFFu : ((1u << f.base) - 1u);

    std::array<uint32_t, BOARD_W> cols{};
    for (int y = 0; y < BOARD_H; ++y) {
        uint16_t r = b.rows[y];
        if (!r) continue;
        uint32_t bit = 1u << y;
        uint16_t m = r;
        while (m) {
            int x = std::countr_zero(m);
            cols[x] |= bit;
            m &= static_cast<uint16_t>(m - 1);
        }
    }

    int min_h = BOARD_H + 1;
    for (int x = 0; x < BOARD_W; ++x) {
        int hx = static_cast<int>(std::bit_width(cols[x]));
        f.h[x] = hx;
        int rx = std::max(0, hx - f.base);
        f.rel[x] = rx;
        f.sum_rel += rx;
        if (hx > f.max_h) f.max_h = hx;
        if (rx > f.max_rel) f.max_rel = rx;
        if (x >= 3 && x <= 6 && hx > f.center_peak) f.center_peak = hx;
        if (hx < min_h) { min_h = hx; f.well_col = x; }

        if (hx > f.base) {
            uint32_t col_below = (hx >= 32) ? 0xFFFFFFFFu : ((1u << hx) - 1u);
            uint32_t gaps = col_below & ~cols[x] & ~floor_mask;
            f.holes += std::popcount(gaps);
            uint32_t g = gaps;
            while (g) {
                int gy = std::countr_zero(g);
                int cov = std::popcount(cols[x] >> (gy + 1));
                f.covered += std::min(cov, 6);
                g &= (g - 1);
            }
            // Column transitions above solid floor
            uint32_t c_shifted = (cols[x] >> f.base);
            // Bottom boundary (solid floor) is filled (1), top above max_h is empty (0)
            uint32_t c_prev = (c_shifted << 1) | 1u;
            uint32_t mask_h = ((hx - f.base) >= 31) ? 0x7FFFFFFFu : ((1u << (hx - f.base + 1)) - 1u);
            f.col_trans += std::popcount((c_shifted ^ c_prev) & mask_h);
        }
    }

    // Row transitions for non-opaque rows in [base, max_h)
    for (int y = f.base; y < f.max_h; ++y) {
        if ((b.opaque_mask >> y) & 1u) continue;
        uint32_t row = (static_cast<uint32_t>(b.rows[y]) << 1) | 1u | (1u << 11);
        f.row_trans += std::popcount((row ^ (row >> 1)) & 0x7FFu);
    }

    // Well structure: explicitly check edge wells (0 and 9) too so ties never hide a clean 9-0 well
    auto well_at = [&](int x) -> int {
        int lh = (x == 0) ? BOARD_H : f.h[x - 1];
        int rh = (x == BOARD_W - 1) ? BOARD_H : f.h[x + 1];
        return std::max(0, std::min(lh, rh) - f.h[x]);
    };
    int best_w_col = f.well_col;
    int best_w_dep = well_at(f.well_col);
    for (int edge : {0, BOARD_W - 1}) {
        int d = well_at(edge);
        if (d > best_w_dep || (d == best_w_dep && d > 0 && best_w_col != 0 && best_w_col != BOARD_W - 1)) {
            best_w_col = edge;
            best_w_dep = d;
        }
    }
    f.well_col = best_w_col;
    f.well_depth = best_w_dep;
    for (int x = 0; x < BOARD_W; ++x) {
        if (x == f.well_col) continue;
        int d = well_at(x);
        if (d >= 3) f.extra_wells += (d - 2);
    }

    // Bumpiness excluding the single chosen well column (if it's a clean well)
    int prev_x = -1;
    for (int x = 0; x < BOARD_W; ++x) {
        if (x == f.well_col && f.well_depth >= 1) continue;
        if (prev_x >= 0) {
            int diff = std::abs(f.rel[x] - f.rel[prev_x]);
            f.bumpiness += diff;
            f.bump_sq += diff * diff;
        }
        prev_x = x;
    }

    // Known garbage hole tracking (real cell==2 rows)
    uint32_t real_garb = b.garbage_mask & ~b.opaque_mask;
    if (real_garb) {
        f.garbage_rows = std::popcount(real_garb);
        int top_g = static_cast<int>(std::bit_width(real_garb)) - 1;
        uint16_t holes_in_top = static_cast<uint16_t>(FULL_ROW & ~b.rows[top_g]);
        if (holes_in_top) {
            int min_cov = 99;
            uint16_t hm = holes_in_top;
            while (hm) {
                int gx = std::countr_zero(hm);
                int cov = std::popcount(cols[gx] >> (top_g + 1));
                if (cov < min_cov) min_cov = cov;
                hm &= static_cast<uint16_t>(hm - 1);
            }
            if (min_cov < 99) f.garbage_cover = min_cov;
        }
    }

    // Active T-Spin Double (TSD) slot & base detection (Medium Bot)
    if (detect_tsd && f.max_h <= 16) {
        int slots = 0;
        int bases = 0;
        for (int x = 1; x <= BOARD_W - 2; ++x) {
            int y = f.h[x];
            if (y < f.base || y + 3 >= BOARD_H) continue;
            // Base row y: notch at x is empty (since y == h[x]), both sides (x-1, y) and (x+1, y) filled
            if (!((b.rows[y] & (1u << (x - 1))) && (b.rows[y] & (1u << (x + 1))))) continue;
            // Row y+1: 3-wide open cavity (x-1, x, x+1 all empty)
            uint16_t mask3 = static_cast<uint16_t>(7u << (x - 1));
            if (b.rows[y + 1] & mask3) continue;
            // Row y+2: check overhang status
            bool left_ov  = (b.rows[y + 2] & (1u << (x - 1))) != 0;
            bool mid_blk  = (b.rows[y + 2] & (1u << x)) != 0;
            bool right_ov = (b.rows[y + 2] & (1u << (x + 1))) != 0;
            if (mid_blk) continue;
            int fill0 = std::popcount(b.rows[y]);
            int fill1 = std::popcount(b.rows[y + 1]);
            if (fill0 < 6 || fill1 < 4) continue;

            if (left_ov != right_ov) {
                // Stage 2: Completed overhang! Check sky is open above entry column.
                int entry_x = left_ov ? (x + 1) : (x - 1);
                if (f.h[entry_x] <= y + 1) {
                    ++slots;
                    if (fill0 >= 8 && fill1 >= 6) ++slots; // imminent ready-to-fire TSD!
                }
            } else if (!left_ov && !right_ov) {
                // Stage 1: Clean T-base with a wall/support adjacent to hang a roof on
                bool can_hang_left = (x >= 2 && f.h[x - 2] >= y + 2);
                bool can_hang_right = (x <= BOARD_W - 3 && f.h[x + 2] >= y + 2);
                if ((can_hang_left || can_hang_right) && f.h[x - 1] <= y + 1 && f.h[x + 1] <= y + 1) {
                    ++bases;
                }
            }
        }
        f.tsd_slots = std::min(slots, 3);
        f.tsd_bases = std::min(bases, 2);
    }

    return f;
}

#if BOT_TIER == 0
// ------------------------- WEAK BOT EVALUATION & SEARCH -------------------------
// Fast, solid 4-ply C++20 9-0 Tetris + downstack + overtime-survival player.
// Beats legacy Python strong cleanly while leaving large headroom for Medium.

inline int eval_weak_board(const BoardState& b, int combo, bool b2b, int8_t hold_piece,
                           uint8_t spawn_mask, const WarningQueue& warnings, int cur_tick) {
    BoardFeatures f = extract_features(b, false);
    int spawnable = std::popcount(spawn_mask);

    int soon_warn = warnings.due_by(cur_tick + 30);
    int eff_danger = f.max_h + std::min(soon_warn, 8);
    bool ot_tight = (f.base >= 10);
    bool crisis = (eff_danger >= 16 || f.center_peak >= 16 || f.base >= 13);
    bool downstack = (crisis || ot_tight || eff_danger >= 13 || f.holes >= 2 || (f.garbage_rows >= 3 && f.max_h >= 9));

    int score = 0;
    if (crisis) {
        score -= 52 * f.sum_rel;
        score -= 42 * f.max_rel;
    } else if (downstack) {
        score -= 24 * f.sum_rel;
        score -= 14 * f.max_rel;
    } else {
        // Low clean stack: freely build 4-7 high for clean 9-0 Tetrises!
        score -= 6 * f.sum_rel;
        if (f.max_rel > 8) {
            int d = f.max_rel - 8;
            score -= 24 * d * d;
        }
    }

    score -= (crisis ? 320 : 380) * f.holes;
    score -= (crisis ? 48 : 42) * f.covered;
    score -= 19 * f.row_trans;
    score -= 25 * f.col_trans;
    score -= 14 * f.bumpiness;
    score -= 3 * f.bump_sq;
    score -= 48 * f.extra_wells;
    if (f.garbage_rows > 0) {
        score -= (downstack ? 105 : 70) * f.garbage_cover;
        if (f.garbage_cover == 0) score += 55;
    }

    if (f.max_h > 14) {
        int d = f.max_h - 14;
        score -= 120 * d * d;
    }
    if (f.center_peak > 13) {
        int d = f.center_peak - 13;
        score -= 160 * d * d;
    }
    // Clean 9-0 edge well for Tetris stacking (disabled when tight/overtime)
    if (!downstack && f.holes <= 1 && (f.well_col == 0 || f.well_col == 9)) {
        score += 56 * std::min(f.well_depth, 5);
    }
    score += 95 * (b2b ? 1 : 0);
    score += 24 * std::min(combo, 6);
    if (hold_piece == P_I) score += 35;
    score -= 1600 * (7 - spawnable);
    return score;
}

inline int eval_weak_move_reward(const Placement& pl, const SettlementResult& sr,
                                 bool was_b2b, int pre_holes, int pre_garb_rows, int pre_base) {
    bool ot_tight = (pre_base >= 10);
    bool helped_dig = (ot_tight || pre_garb_rows > 0 || pre_holes > 0);
    int r = 0;
    r += 185 * sr.attack;
    if (sr.cancelled > 0) r += 35 * sr.cancelled;
    if (pl.perfect_clear) r += 1600;
    else if (pl.lines == 4) {
        r += 175 + (was_b2b ? 65 : 0);
    } else if (pl.spin == SPIN_FULL) {
        r += 140 + (was_b2b ? 50 : 0);
    } else if (pl.lines == 1 && sr.attack == 0 && !sr.combo_blocked && sr.combo < 2 && !helped_dig) {
        r -= 130; // avoid wasteful 0-attack single clears when clean
    }
    if (ot_tight && pl.lines > 0) r += 40 * pl.lines; // encourage clean overtime skimming
    if (sr.combo_blocked) r += 85;
    if (sr.risen > 0) r -= 45 * sr.risen;
    if (was_b2b && !sr.b2b && !helped_dig && !sr.combo_blocked) r -= 85;
    r -= 3 * sr.total_frames;
    return r;
}

#else
// ------------------------- MEDIUM BOT EVALUATION & SEARCH -------------------------
// Elite 6-ply Next-5 Beam Search with full Hold tracking, active T-Spin (TSD/TST)
// setup & execution, B2B & Combo mastery, tactical Combo Blocking, overtime survival,
// and opponent kill-pressure awareness.

inline int eval_medium_board(const BoardState& b, int combo, bool b2b, int8_t hold_piece,
                             uint8_t spawn_mask, const WarningQueue& warnings, int cur_tick,
                             const EvalContext& ctx) {
    BoardFeatures f = extract_features(b, true);
    int spawnable = std::popcount(spawn_mask);

    int soon_warn = warnings.due_by(cur_tick + 35);
    int eff_danger = f.max_h + std::min(soon_warn, 8);
    bool ot_tight = (f.base >= 10);
    bool ot_extreme = (f.base >= 15);
    bool crisis = (eff_danger >= 16 || f.center_peak >= 16 || f.base >= 13);
    bool downstack = (crisis || ot_tight || eff_danger >= 13 || f.holes >= 2 || (f.garbage_rows >= 3 && f.max_h >= 9));

    int score = 0;

    // Height & danger profile: low clean stack is ammo, NOT liability!
    if (ot_extreme) {
        // Extreme overtime (<= 9 rows left): absolute priority on flat low center & zero holes
        score -= 85 * f.sum_rel;
        score -= 110 * f.max_rel;
        int center_rel = std::max(0, f.center_peak - f.base);
        score -= 140 * center_rel * center_rel;
    } else if (crisis) {
        score -= 58 * f.sum_rel;
        score -= 48 * f.max_rel;
    } else if (downstack) {
        score -= 26 * f.sum_rel;
        score -= 18 * f.max_rel;
    } else {
        score -= 4 * f.sum_rel;
        if (f.max_rel > 8) {
            int d = f.max_rel - 8;
            score -= 28 * d * d;
        }
    }

    // Hard top-out / spawn-zone protection (with +1 overtime solid-row safety buffer when base >= 14)
    int ot_buf = (f.base >= 14) ? 1 : 0;
    if (f.max_h + ot_buf > 14) {
        int d = f.max_h + ot_buf - 14;
        score -= (ot_extreme ? 240 : 130) * d * d;
    }
    if (f.center_peak + ot_buf > 13) {
        int d = f.center_peak + ot_buf - 13;
        score -= (ot_extreme ? 380 : 175) * d * d;
    }
    if (ot_extreme && f.center_peak >= 19) {
        score -= 4000; // never let center enter row 19+ right before an overtime solid row!
    }
    score -= (ot_extreme ? 3500 : 1800) * (7 - spawnable);

    // Holes, covered cells, transitions
    int hole_w = ot_extreme ? 580 : (crisis ? 330 : (downstack ? 380 : 460));
    int cov_w  = ot_extreme ? 85  : (crisis ? 55  : (downstack ? 68  : 50));
    score -= hole_w * f.holes;
    score -= cov_w * f.covered;
    score -= (downstack ? 24 : 20) * f.row_trans;
    score -= (downstack ? 32 : 28) * f.col_trans;

    // Surface smoothness (excluding clean well)
    score -= (downstack ? 12 : 16) * f.bumpiness;
    score -= (downstack ? 2  : 4)  * f.bump_sq;
    score -= 62 * f.extra_wells;

    // Garbage downstacking clarity (huge in modern vs!)
    if (f.garbage_rows > 0) {
        score -= (downstack ? 140 : 85) * f.garbage_cover;
        if (f.garbage_cover == 0) score += 105; // hole completely open & ready to spike/combo!
    }

    // Well structure for Tetris / 6-3 / 9-0 B2B stacking
    if (!downstack && f.holes <= 1) {
        bool edge_well = (f.well_col == 0 || f.well_col == 9);
        bool mid_well  = (f.well_col == 2 || f.well_col == 3 || f.well_col == 6 || f.well_col == 7);
        int well_w = edge_well ? 56 : (mid_well ? 52 : 20);
        int capped_d = std::min(f.well_depth, 6);
        score += well_w * capped_d;
        if (f.well_depth >= 4 && ctx.has_i_soon && (edge_well || mid_well)) score += 70;
    }

    // Active T-Spin Double (TSD) slot & base shaping reward
    if (!crisis && !ot_tight) {
        if (f.tsd_slots > 0) {
            int tsd_w = ctx.has_t_soon ? 260 : 145;
            if (b2b) tsd_w += 65;
            score += tsd_w * f.tsd_slots;
        }
        if (f.tsd_bases > 0 && ctx.has_t_soon) {
            score += 68 * f.tsd_bases;
        }
    }

    // B2B & Combo state value
    if (b2b) score += 185;
    if (combo >= 1) {
        static constexpr int combo_val[10] = {0, 38, 98, 180, 275, 380, 490, 600, 710, 820};
        score += combo_val[std::min(combo, 9)];
    }

    // Holding T or I is a real strategic weapon
    if (hold_piece == P_T) score += 90;
    else if (hold_piece == P_I) score += 60;

    // Uncancelled imminent warning burden
    int rem_warn = warnings.total_lines();
    if (rem_warn > 0) score -= (crisis ? 95 : 42) * rem_warn;

    return score;
}

inline int eval_medium_move_reward(const Placement& pl, const SettlementResult& sr,
                                   bool was_b2b, int pre_holes, int pre_garb_rows, int pre_base, int pre_max_h,
                                   const EvalContext& ctx) {
    bool ot_tight = (pre_base >= 10);
    bool ot_extreme = (pre_base >= 15);
    // Opponent kill pressure multiplier (in extreme overtime, survival beats greedy stacking!)
    int atk_w = ot_extreme ? 180
              : (ctx.opp_max_h >= 14 ? 300 : (ctx.opp_max_h >= 11 ? 260 : 230));
    int r = atk_w * sr.attack;

    // Spike burst bonus (sending 4+ net lines in one shot is far deadlier than trickling 1s)
    if (!ot_extreme && sr.sent >= 4) r += 40 * (sr.sent - 3);

    // Bonus for efficient garbage cancellation when threatened
    if (sr.cancelled > 0) {
        r += (pre_max_h >= 13 ? 130 : 65) * sr.cancelled;
    }

    // Strong reward for attack-efficient difficult clears (TSD/TST/Tetris/PC)
    if (pl.perfect_clear) {
        r += 2400;
    } else if (pl.spin == SPIN_FULL) {
        if (pl.lines == 3) r += ot_extreme ? 220 : 580;
        else if (pl.lines == 2) r += ot_extreme ? 180 : 440;
        else r += ot_extreme ? 120 : 200;
        if (was_b2b && !ot_extreme) r += 140;
    } else if (pl.lines == 4) {
        r += ot_extreme ? 150 : 230;
        if (was_b2b && !ot_extreme) r += 105;
    } else if (pl.lines > 0) {
        bool helped_dig = (ot_tight || pre_garb_rows > 0 || pre_holes > 0);
        if (pl.lines == 1 && sr.attack == 0 && !sr.combo_blocked && sr.combo < 2 && !helped_dig) {
            r -= 200; // strongly reject slow 0-attack normal singles when clean!
        } else if (pl.lines == 2 && sr.combo < 2 && !helped_dig && !sr.combo_blocked) {
            r -= 50;  // save clean lines for Tetris/TSD
        }
        if (was_b2b && !sr.b2b && !helped_dig && !sr.combo_blocked) {
            r -= 165; // protect B2B chain!
        }
        if (ot_extreme) r += 110 * pl.lines; // keep board razor-low in extreme overtime
        else if (ot_tight) r += 55 * pl.lines;
    }

    // Danger-scaled tactical combo blocking & garbage rise defense
    if (sr.combo_blocked) {
        r += (pre_max_h >= 13) ? 280 : 120;
    }
    if (sr.risen > 0) {
        int rise_w = (pre_max_h + sr.risen >= 16) ? 210 : 65;
        r -= rise_w * sr.risen;
    }

    // High-PPS finesse efficiency: fast routes everywhere, without punishing +16 special clear animations!
    if (pl.spin == SPIN_FULL || pl.lines == 4) {
        r -= 2 * static_cast<int>(pl.pre_lock_frames);
    } else if (pl.lines > 0 && sr.attack == 0 && !ot_tight) {
        r -= 4 * sr.total_frames;
    } else {
        r -= 4 * static_cast<int>(pl.pre_lock_frames);
    }

    return r;
}
#endif

// ------------------------- Bot Controller -------------------------

struct GameObs {
    int turn = 1;
    int frame = 0;
    BoardState self_board;
    int8_t current = P_NONE;
    int8_t next = P_NONE;
    std::array<int8_t, 6> queue{};
    int queue_len = 0;
    int8_t hold = P_NONE;
    bool can_hold = true;
    WarningQueue warnings;
    int combo = 0;
    bool b2b = false;
    int combo_blocks = 0;
    int opp_max_h = 0;
    int opp_holes = 0;
};

struct Decision {
    bool is_hold = false;
    std::vector<uint8_t> commands;
};

class Bot {
public:
    RulesConfig cfg;
    MovegenScratch scratch;

    // HOLD -> PLAY cache for instant second-half reply
    bool has_cached_hold = false;
    int cached_turn = -1;
    int8_t cached_piece = P_NONE;
    uint64_t cached_board_hash = 0;
    std::vector<uint8_t> cached_cmds;

    void parse_board(const Json* jboard, BoardState& b, int* out_max_h = nullptr, int* out_holes = nullptr) {
        b = BoardState{};
        if (!jboard || jboard->type != Json::ARR) return;
        int max_h = 0;
        std::array<uint32_t, BOARD_W> cols{};
        int nrows = std::min<int>(BOARD_H, static_cast<int>(jboard->arr.size()));
        for (int y = 0; y < nrows; ++y) {
            const auto& row = jboard->arr[y];
            if (row.type != Json::ARR) continue;
            uint16_t mask = 0;
            bool is_solid = false;
            bool is_garb = false;
            int ncols = std::min<int>(BOARD_W, static_cast<int>(row.arr.size()));
            for (int x = 0; x < ncols; ++x) {
                int v = row.arr[x].as_int(0);
                if (v != 0) {
                    mask |= static_cast<uint16_t>(1u << x);
                    cols[x] |= (1u << y);
                    if (v == 3) is_solid = true;
                    else if (v == 2) is_garb = true;
                }
            }
            b.rows[y] = mask;
            if (mask) max_h = y + 1;
            if (is_solid) b.solid_mask |= (1u << y);
            if (is_garb) b.garbage_mask |= (1u << y);
        }
        if (out_max_h) *out_max_h = max_h;
        if (out_holes) {
            int holes = 0;
            for (int x = 0; x < BOARD_W; ++x) {
                int hx = static_cast<int>(std::bit_width(cols[x]));
                if (hx > 0) {
                    uint32_t below = (hx >= 32) ? 0xFFFFFFFFu : ((1u << hx) - 1u);
                    holes += std::popcount(below & ~cols[x]);
                }
            }
            *out_holes = holes;
        }
    }

    GameObs parse_observation(const Json& obs) {
        if (obs.get("rules") || obs.get("geometry")) {
            cfg.load_from_json(obs.get("rules"), obs.get("geometry"));
        }
        GameObs g;
        if (auto* v = obs.get("turn")) g.turn = v->as_int(1);
        if (auto* v = obs.get("frame")) g.frame = v->as_int(0);
        else g.frame = g.turn;

        if (auto* self = obs.get("self"); self && self->type == Json::OBJ) {
            parse_board(self->get("board"), g.self_board);
            if (auto* v = self->get("current"); v && v->type == Json::STR && !v->sval.empty())
                g.current = char_to_piece(v->sval[0]);
            if (auto* v = self->get("next"); v && v->type == Json::STR && !v->sval.empty())
                g.next = char_to_piece(v->sval[0]);
            if (auto* nq = self->get("next_queue"); nq && nq->type == Json::ARR) {
                for (const auto& e : nq->arr) {
                    if (e.type == Json::STR && !e.sval.empty() && g.queue_len < 6) {
                        g.queue[g.queue_len++] = char_to_piece(e.sval[0]);
                    }
                }
            }
            if (g.queue_len == 0 && g.next >= 0) {
                g.queue[g.queue_len++] = g.next;
            }
            if (auto* v = self->get("hold"); v && v->type == Json::STR && !v->sval.empty())
                g.hold = char_to_piece(v->sval[0]);
            if (auto* v = self->get("can_hold")) g.can_hold = v->as_bool(true);
            if (auto* v = self->get("combo")) g.combo = v->as_int(0);
            if (auto* v = self->get("b2b")) g.b2b = v->as_bool(false);
            if (auto* v = self->get("combo_blocks")) g.combo_blocks = v->as_int(0);
            if (auto* ws = self->get("warnings"); ws && ws->type == Json::ARR) {
                for (const auto& w : ws->arr) {
                    if (w.type == Json::OBJ && g.warnings.count < 8) {
                        int16_t ln = static_cast<int16_t>(w.get("lines") ? w.get("lines")->as_int(0) : 0);
                        int32_t due = w.get("due") ? w.get("due")->as_int(0) : 0;
                        if (ln > 0) g.warnings.items[g.warnings.count++] = {ln, due};
                    }
                }
            }
        }
        if (auto* opp = obs.get("opponent"); opp && opp->type == Json::OBJ) {
            BoardState dummy;
            parse_board(opp->get("board"), dummy, &g.opp_max_h, &g.opp_holes);
        }
        return g;
    }

    Decision decide(const GameObs& g, Clock::time_point deadline) {
        // Fast path: execute cached HOLD -> PLAY move
        uint64_t bhash = g.self_board.hash64();
        if (has_cached_hold && !g.can_hold &&
            cached_turn == g.turn && cached_piece == g.current && cached_board_hash == bhash) {
            has_cached_hold = false;
            return {false, cached_cmds};
        }
        has_cached_hold = false;

        int now_tick = cfg.is_realtime ? g.frame : g.turn;

#if BOT_TIER == 0
        // ================== WEAK BOT: 4-ply Beam Search with Hold ==================
        struct RootAction {
            bool used_hold = false;
            std::vector<uint8_t> cmds;
            int root_eval = -1000000000;
        };
        struct Node {
            BoardState board;
            WarningQueue warnings;
            int root_idx = 0;
            int8_t hold = P_NONE;
            int8_t q_idx = 0;
            int combo = 0;
            bool b2b = false;
            int combo_blocks = 0;
            int tick = 0;
            int cum_reward = 0;
            int score = -1000000000;
            uint8_t spawn_mask = 0x7F;
            uint64_t dedup_key = 0;
        };

        auto pre_feat = extract_features(g.self_board, false);
        std::vector<RootAction> root_actions;
        std::vector<Node> beam;
        std::vector<Placement> moves;

        auto expand_root = [&](int8_t piece, bool used_hold, int8_t new_hold, int8_t next_q_idx) {
            if (piece < 0) return;
            generate_moves_full(cfg, g.self_board, piece, true, scratch, moves);
            int8_t next_p = (next_q_idx < g.queue_len) ? g.queue[next_q_idx] : P_NONE;
            for (auto& pl : moves) {
                auto sr = apply_settlement(cfg, pl, g.warnings, g.combo, g.b2b, g.combo_blocks,
                                           now_tick, used_hold, next_p);
                int rew = eval_weak_move_reward(pl, sr, g.b2b, pre_feat.holes, pre_feat.garbage_rows, pre_feat.base);
                int bval = sr.alive ? eval_weak_board(sr.board, sr.combo, sr.b2b, new_hold,
                                                      sr.spawn_mask, sr.warnings, sr.next_frame)
                                    : -1000000000;
                int total = sr.alive ? (bval + rew) : -1000000000;
                int ridx = static_cast<int>(root_actions.size());
                root_actions.push_back({used_hold, std::move(pl.cmds), total});
                if (sr.alive) {
                    Node nd;
                    nd.board = sr.board;
                    nd.warnings = sr.warnings;
                    nd.root_idx = ridx;
                    nd.hold = new_hold;
                    nd.q_idx = next_q_idx;
                    nd.combo = sr.combo;
                    nd.b2b = sr.b2b;
                    nd.combo_blocks = sr.combo_blocks;
                    nd.tick = sr.next_frame;
                    nd.cum_reward = rew;
                    nd.score = total;
                    nd.spawn_mask = sr.spawn_mask;
                    nd.dedup_key = sr.board.hash64() ^ (static_cast<uint64_t>(new_hold + 2) * 0x9E3779B97F4A7C15ULL)
                                   ^ (static_cast<uint64_t>(sr.b2b) << 61)
                                   ^ (static_cast<uint64_t>(next_q_idx) << 53);
                    beam.push_back(std::move(nd));
                }
            }
        };

        expand_root(g.current, false, g.hold, 0);
        if (g.can_hold) {
            if (g.hold >= 0) {
                if (g.hold != g.current) expand_root(g.hold, true, g.current, 0);
            } else if (g.queue_len > 0) {
                if (g.queue[0] != g.current) expand_root(g.queue[0], true, g.current, 1);
            }
        }

        if (root_actions.empty()) return {false, {CMD_HARD_DROP}};

        int best_root_idx = 0;
        int best_root_score = root_actions[0].root_eval;
        for (int i = 1; i < static_cast<int>(root_actions.size()); ++i) {
            if (root_actions[i].root_eval > best_root_score ||
                (root_actions[i].root_eval == best_root_score &&
                 !root_actions[i].used_hold && root_actions[best_root_idx].used_hold)) {
                best_root_score = root_actions[i].root_eval;
                best_root_idx = i;
            }
        }

        auto prune_beam = [](std::vector<Node>& nodes, int width) {
            std::stable_sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
                return a.score > b.score;
            });
            std::vector<Node> kept;
            kept.reserve(static_cast<size_t>(width));
            std::unordered_map<uint64_t, bool> seen;
            seen.reserve(static_cast<size_t>(width) * 2);
            for (auto& nd : nodes) {
                if (seen.find(nd.dedup_key) != seen.end()) continue;
                seen.emplace(nd.dedup_key, true);
                kept.push_back(std::move(nd));
                if (static_cast<int>(kept.size()) >= width) break;
            }
            nodes = std::move(kept);
        };

        // 3-ply total (2 extra plies), compact beam, fast hard-drop only on inner plies (no T-spin building)
        constexpr int BEAM_WIDTHS[2] = {12, 8};
        const int max_extra = (g.hold >= 0) ? 2 : std::min(1, std::max(0, g.queue_len - 1));
        prune_beam(beam, BEAM_WIDTHS[0]);

        std::vector<Node> next_beam;
        next_beam.reserve(512);
        for (int depth = 0; depth < max_extra && !beam.empty(); ++depth) {
            if (Clock::now() >= deadline) break;
            next_beam.clear();
            bool any_exp = false, timed_out = false;
            for (const auto& nd : beam) {
                if (Clock::now() >= deadline) { timed_out = true; break; }
                if (nd.q_idx >= g.queue_len) continue;
                any_exp = true;
                int8_t cur_p = g.queue[nd.q_idx];
                int8_t nxt_idx = static_cast<int8_t>(nd.q_idx + 1);
                auto nf = extract_features(nd.board, false);

                auto expand_child = [&](int8_t piece, bool use_hold, int8_t new_hold, int8_t child_q_idx) {
                    if (piece < 0) return;
                    generate_moves_fast(cfg, nd.board, piece, moves);
                    int8_t look_next = (child_q_idx < g.queue_len) ? g.queue[child_q_idx] : P_NONE;
                    for (const auto& pl : moves) {
                        auto sr = apply_settlement(cfg, pl, nd.warnings, nd.combo, nd.b2b,
                                                   nd.combo_blocks, nd.tick, use_hold, look_next);
                        if (!sr.alive) continue;
                        int rew = eval_weak_move_reward(pl, sr, nd.b2b, nf.holes, nf.garbage_rows, nf.base);
                        int disc_rew = (rew * (9 - depth)) / 10;
                        int cum = nd.cum_reward + disc_rew;
                        int bval = eval_weak_board(sr.board, sr.combo, sr.b2b, new_hold,
                                                   sr.spawn_mask, sr.warnings, sr.next_frame);
                        Node c;
                        c.board = sr.board;
                        c.warnings = sr.warnings;
                        c.root_idx = nd.root_idx;
                        c.hold = new_hold;
                        c.q_idx = child_q_idx;
                        c.combo = sr.combo;
                        c.b2b = sr.b2b;
                        c.combo_blocks = sr.combo_blocks;
                        c.tick = sr.next_frame;
                        c.cum_reward = cum;
                        c.score = bval + cum;
                        c.spawn_mask = sr.spawn_mask;
                        c.dedup_key = sr.board.hash64() ^ (static_cast<uint64_t>(new_hold + 2) * 0x9E3779B97F4A7C15ULL)
                                      ^ (static_cast<uint64_t>(sr.b2b) << 61)
                                      ^ (static_cast<uint64_t>(child_q_idx) << 53);
                        next_beam.push_back(std::move(c));
                    }
                };
                expand_child(cur_p, false, nd.hold, nxt_idx);
                if (nd.hold >= 0) {
                    if (nd.hold != cur_p) expand_child(nd.hold, true, cur_p, nxt_idx);
                } else if (nxt_idx < g.queue_len && g.queue[nxt_idx] != cur_p) {
                    expand_child(g.queue[nxt_idx], true, cur_p, static_cast<int8_t>(nxt_idx + 1));
                }
            }
            if (!any_exp || timed_out || next_beam.empty()) break;
            int next_w = (depth + 1 < max_extra) ? BEAM_WIDTHS[depth + 1] : 8;
            prune_beam(next_beam, next_w);
            beam = std::move(next_beam);
            if (!beam.empty()) {
                best_root_idx = beam[0].root_idx;
                best_root_score = beam[0].score;
            }
        }

        const auto& chosen = root_actions[best_root_idx];
        if (chosen.used_hold) {
            has_cached_hold = true;
            cached_turn = g.turn;
            cached_piece = (g.hold >= 0) ? g.hold : (g.queue_len > 0 ? g.queue[0] : P_NONE);
            cached_board_hash = bhash;
            cached_cmds = chosen.cmds;
            return {true, {}};
        }
        return {false, chosen.cmds};

#else
        // ================== MEDIUM BOT: 5-6 Ply Next-5 Beam Search ==================
        struct RootAction {
            bool used_hold = false;
            std::vector<uint8_t> cmds;
            int root_eval = -1000000000;
        };

        struct Node {
            BoardState board;
            WarningQueue warnings;
            int root_idx = 0;
            int8_t hold = P_NONE;
            int8_t q_idx = 0; // next index in g.queue to consume
            int combo = 0;
            bool b2b = false;
            int combo_blocks = 0;
            int tick = 0;
            int cum_reward = 0;
            int score = -1000000000;
            uint8_t spawn_mask = 0x7F;
            bool alive = true;
            uint64_t dedup_key = 0;
        };

        EvalContext ctx;
        ctx.opp_max_h = g.opp_max_h;
        ctx.opp_holes = g.opp_holes;
        ctx.incoming_total = g.warnings.total_lines();

        auto set_soon_flags = [&](int8_t hold_p, int q_start) {
            ctx.has_t_soon = (hold_p == P_T);
            ctx.has_i_soon = (hold_p == P_I);
            for (int k = q_start; k < g.queue_len && k < q_start + 3; ++k) {
                if (g.queue[k] == P_T) ctx.has_t_soon = true;
                if (g.queue[k] == P_I) ctx.has_i_soon = true;
            }
        };

        auto pre_feat = extract_features(g.self_board, false);
        std::vector<RootAction> root_actions;
        std::vector<Node> beam;
        std::vector<Placement> moves;

        auto expand_root = [&](int8_t piece, bool used_hold, int8_t new_hold, int8_t next_q_idx) {
            if (piece < 0) return;
            generate_moves_full(cfg, g.self_board, piece, true, scratch, moves);
            int8_t next_p = (next_q_idx < g.queue_len) ? g.queue[next_q_idx] : P_NONE;
            set_soon_flags(new_hold, next_q_idx);
            for (auto& pl : moves) {
                auto sr = apply_settlement(cfg, pl, g.warnings, g.combo, g.b2b, g.combo_blocks,
                                           now_tick, used_hold, next_p);
                int rew = eval_medium_move_reward(pl, sr, g.b2b, pre_feat.holes, pre_feat.garbage_rows, pre_feat.base, pre_feat.max_h, ctx);
                int bval = sr.alive ? eval_medium_board(sr.board, sr.combo, sr.b2b, new_hold,
                                                        sr.spawn_mask, sr.warnings, sr.next_frame, ctx)
                                    : -1000000000;
                int total = sr.alive ? (bval + rew) : -1000000000;
                int ridx = static_cast<int>(root_actions.size());
                root_actions.push_back({used_hold, std::move(pl.cmds), total});

                if (sr.alive) {
                    Node nd;
                    nd.board = sr.board;
                    nd.warnings = sr.warnings;
                    nd.root_idx = ridx;
                    nd.hold = new_hold;
                    nd.q_idx = next_q_idx;
                    nd.combo = sr.combo;
                    nd.b2b = sr.b2b;
                    nd.combo_blocks = sr.combo_blocks;
                    nd.tick = sr.next_frame;
                    nd.cum_reward = rew;
                    nd.score = total;
                    nd.spawn_mask = sr.spawn_mask;
                    nd.alive = true;
                    nd.dedup_key = sr.board.hash64() ^ (static_cast<uint64_t>(new_hold + 2) * 0x9E3779B97F4A7C15ULL)
                                   ^ (static_cast<uint64_t>(sr.b2b) << 61)
                                   ^ (static_cast<uint64_t>(std::min(sr.combo, 7)) << 57)
                                   ^ (static_cast<uint64_t>(next_q_idx) << 53);
                    beam.push_back(std::move(nd));
                }
            }
        };

        // Branch 1: play current piece directly
        expand_root(g.current, false, g.hold, 0);
        // Branch 2: HOLD
        if (g.can_hold) {
            if (g.hold >= 0) {
                if (g.hold != g.current) expand_root(g.hold, true, g.current, 0);
            } else if (g.queue_len > 0) {
                if (g.queue[0] != g.current) expand_root(g.queue[0], true, g.current, 1);
            }
        }

        if (root_actions.empty()) {
            return {false, {CMD_HARD_DROP}};
        }

        // Best fallback from depth 1
        int best_root_idx = 0;
        int best_root_score = root_actions[0].root_eval;
        for (int i = 1; i < static_cast<int>(root_actions.size()); ++i) {
            if (root_actions[i].root_eval > best_root_score ||
                (root_actions[i].root_eval == best_root_score &&
                 !root_actions[i].used_hold && root_actions[best_root_idx].used_hold)) {
                best_root_score = root_actions[i].root_eval;
                best_root_idx = i;
            }
        }

        // Prune & deduplicate helper
        auto prune_beam = [](std::vector<Node>& nodes, int width) {
            std::stable_sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
                return a.score > b.score;
            });
            std::vector<Node> kept;
            kept.reserve(static_cast<size_t>(width));
            std::unordered_map<uint64_t, bool> seen;
            seen.reserve(static_cast<size_t>(width) * 2);
            for (auto& nd : nodes) {
                if (seen.find(nd.dedup_key) != seen.end()) continue;
                seen.emplace(nd.dedup_key, true);
                kept.push_back(std::move(nd));
                if (static_cast<int>(kept.size()) >= width) break;
            }
            nodes = std::move(kept);
        };

        // Depth-adaptive beam widths (equal horizon across PLAY and empty-HOLD branches!)
        constexpr int BEAM_WIDTHS[5] = {54, 44, 36, 28, 22};
        const int max_extra_plies = (g.hold >= 0) ? 5 : std::min(4, std::max(0, g.queue_len - 1));

        prune_beam(beam, BEAM_WIDTHS[0]);

        std::vector<Node> next_beam;
        next_beam.reserve(2048);

        for (int depth = 0; depth < max_extra_plies && !beam.empty(); ++depth) {
            if (Clock::now() >= deadline) break;

            next_beam.clear();
            bool any_expanded = false;
            bool timed_out = false;

            for (const auto& nd : beam) {
                if (Clock::now() >= deadline) { timed_out = true; break; }
                if (nd.q_idx >= g.queue_len) continue;
                any_expanded = true;

                int8_t cur_p = g.queue[nd.q_idx];
                int8_t nxt_idx = static_cast<int8_t>(nd.q_idx + 1);

                // Quick pre-state info for reward evaluation
                auto nf = extract_features(nd.board, false);
                bool need_tucks = (nf.holes >= 2 || nf.garbage_rows >= 2 || nf.max_h >= 13);

                auto expand_child = [&](int8_t piece, bool use_hold, int8_t new_hold, int8_t child_q_idx) {
                    if (piece < 0) return;
                    // Full spin/tuck movegen for T pieces, deep-overtime low-ceiling survival, and ply 1 digs
                    bool full_gen = (piece == P_T)
                                    || (nf.base >= 14 && depth < 3)
                                    || (nf.max_h >= 17 && depth < 2)
                                    || (depth == 0 && need_tucks && piece != P_O && piece != P_I);
                    if (full_gen) {
                        generate_moves_full(cfg, nd.board, piece, false, scratch, moves);
                    } else {
                        generate_moves_fast(cfg, nd.board, piece, moves);
                    }
                    int8_t look_next = (child_q_idx < g.queue_len) ? g.queue[child_q_idx] : P_NONE;
                    set_soon_flags(new_hold, child_q_idx);

                    for (const auto& pl : moves) {
                        auto sr = apply_settlement(cfg, pl, nd.warnings, nd.combo, nd.b2b,
                                                   nd.combo_blocks, nd.tick, use_hold, look_next);
                        if (!sr.alive) continue;
                        int rew = eval_medium_move_reward(pl, sr, nd.b2b, nf.holes, nf.garbage_rows, nf.base, nf.max_h, ctx);
                        // Slight discount on deeper move rewards so earlier attacks are preferred
                        int disc_rew = (rew * (10 - depth)) / 10;
                        int cum = nd.cum_reward + disc_rew;
                        int bval = eval_medium_board(sr.board, sr.combo, sr.b2b, new_hold,
                                                     sr.spawn_mask, sr.warnings, sr.next_frame, ctx);
                        Node c;
                        c.board = sr.board;
                        c.warnings = sr.warnings;
                        c.root_idx = nd.root_idx;
                        c.hold = new_hold;
                        c.q_idx = child_q_idx;
                        c.combo = sr.combo;
                        c.b2b = sr.b2b;
                        c.combo_blocks = sr.combo_blocks;
                        c.tick = sr.next_frame;
                        c.cum_reward = cum;
                        c.score = bval + cum;
                        c.spawn_mask = sr.spawn_mask;
                        c.alive = true;
                        c.dedup_key = sr.board.hash64()
                                      ^ (static_cast<uint64_t>(new_hold + 2) * 0x9E3779B97F4A7C15ULL)
                                      ^ (static_cast<uint64_t>(sr.b2b) << 61)
                                      ^ (static_cast<uint64_t>(std::min(sr.combo, 7)) << 57)
                                      ^ (static_cast<uint64_t>(child_q_idx) << 53);
                        next_beam.push_back(std::move(c));
                    }
                };

                // Option A: play queue piece without hold
                expand_child(cur_p, false, nd.hold, nxt_idx);

                // Option B: use hold
                if (nd.hold >= 0) {
                    if (nd.hold != cur_p) expand_child(nd.hold, true, cur_p, nxt_idx);
                } else if (nxt_idx < g.queue_len) {
                    if (g.queue[nxt_idx] != cur_p) {
                        expand_child(g.queue[nxt_idx], true, cur_p, static_cast<int8_t>(nxt_idx + 1));
                    }
                }
            }

            if (!any_expanded || timed_out || next_beam.empty()) break;

            int next_w = (depth + 1 < max_extra_plies) ? BEAM_WIDTHS[depth + 1] : 16;
            prune_beam(next_beam, next_w);
            beam = std::move(next_beam);

            // Commit completed ply's best root choice
            if (!beam.empty()) {
                best_root_idx = beam[0].root_idx;
                best_root_score = beam[0].score;
            }
        }

        const auto& chosen = root_actions[best_root_idx];
        if (chosen.used_hold) {
            has_cached_hold = true;
            cached_turn = g.turn;
            cached_piece = (g.hold >= 0) ? g.hold : (g.queue_len > 0 ? g.queue[0] : P_NONE);
            cached_board_hash = bhash;
            cached_cmds = chosen.cmds;
            return {true, {}};
        }
        return {false, chosen.cmds};
#endif
    }
};

} // namespace tetra

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    tetra::Bot bot;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto started = tetra::Clock::now();

        tetra::Json req;
        tetra::JsonParser parser(line);
        if (!parser.parse(req)) continue;

        std::string req_id;
        if (auto* rid = req.get("request_id"); rid && rid->type == tetra::Json::STR) {
            req_id = rid->sval;
        }
        auto* obs = req.get("observation");
        if (!obs || obs->type != tetra::Json::OBJ) {
            std::cout << "{\"type\":\"PLAY\",\"request_id\":\"" << tetra::json_escape(req_id)
                      << "\",\"commands\":[\"HARD_DROP\"]}\n" << std::flush;
            continue;
        }

        // Compute search deadline: untimed (deterministic) when remaining_ms is absent/null,
        // otherwise stay comfortably inside the 30 ms free think window so think_frames == 0!
        const auto* rem_j = req.get("remaining_ms");
        tetra::Clock::time_point deadline = tetra::Clock::time_point::max();
        if (rem_j && rem_j->type == tetra::Json::NUM) {
            int remaining_ms = rem_j->as_int(100);
            int bank_ms = (req.get("bank_ms") && req.get("bank_ms")->type == tetra::Json::NUM)
                              ? req.get("bank_ms")->as_int(-1) : -1;
            int turn_budget = remaining_ms;
            if (bank_ms >= 0) {
                turn_budget = std::min(remaining_ms, std::max(0, remaining_ms - bank_ms) + std::min(bank_ms, 80));
            }
#if BOT_TIER == 0
            int target_ms = std::min(10, std::max(0, turn_budget - 10));
#else
            int free_cap = std::max(12, bot.cfg.free_think_ms - 8);
            int target_ms = std::min(free_cap, std::max(0, turn_budget - 12));
#endif
            deadline = started + std::chrono::milliseconds(target_ms);
        }

        tetra::GameObs g = bot.parse_observation(*obs);
        tetra::Decision dec = bot.decide(g, deadline);

        std::cout << "{\"type\":\"" << (dec.is_hold ? "HOLD" : "PLAY")
                  << "\",\"request_id\":\"" << tetra::json_escape(req_id) << "\"";
        if (!dec.is_hold) {
            std::cout << ",\"commands\":[";
            if (dec.commands.empty()) {
                std::cout << "\"HARD_DROP\"";
            } else {
                for (size_t i = 0; i < dec.commands.size(); ++i) {
                    if (i > 0) std::cout << ',';
                    std::cout << '"' << tetra::CMD_NAMES[dec.commands[i]] << '"';
                }
            }
            std::cout << "]";
        }
        std::cout << "}\n" << std::flush;
    }
    return 0;
}
