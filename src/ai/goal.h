#pragma once
#include <QDebug>
#include <vector>
#include <initializer_list>

#include "vector.h"

namespace bm {

class World;
class ComponentObject;

namespace ai {

class Tribool {
public:
    typedef enum { False = 0, True = 1, N_A = 2 } Value;

    explicit Tribool(): val_(False) {}
    explicit Tribool(bool x): val_(x ? True : False) {}
    Tribool(Value v): val_(v) {}

    bool is_false() const { return val_ == False; }
    bool is_true() const { return val_ == True; }
    bool is_NA() const { return val_ == N_A; }

private:
    Value val_;
};

// Desired effects
enum class MetricType: uint16_t {
    // Creature is in hand's reach. Position is in the context, passed
    // separately
    MeleeRange,
    // Block was extracted using tools or blasted in some way. Position is in
    // the context, passed separately
    BlockIsNotSolid,
    HaveLeg,        // Creature has at least one leg
    HaveHand,       // Creature has at least one hand
    HaveMiningPick, // Creature has a tool
};

QDebug operator<< (QDebug d, MetricType mt);

// A 3d vector with trivial ctor (unlike Vec3i)
class Pos3i {
public:
    int32_t x, y, z;
    Pos3i(const Vec3i &v): x(v.getX()), y(v.getY()), z(v.getZ()) {}
    bool operator== (const Pos3i& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

inline QDebug operator<< (QDebug d, const Pos3i& p) {
    d.nospace() << "Pos(" << p.x << "," << p.y << "," << p.z << ")";
    return d;
}

class Value {
public:
    enum class Type: uint8_t {
        NoValue,
        Boolean,
        Position
    };

private:
    union {
        bool  b_;
        Pos3i pos_;
    };
    Type type_;
public:
    Value(const Value &) = default;
    explicit Value(): type_(Type::NoValue) {}
    explicit Value(const Vec3i &p): pos_(p), type_(Type::Position) {}
    explicit Value(bool b): b_(b), type_(Type::Boolean) {}

    bool is_position() const { return type_ == Type::Position; }
    Vec3i get_pos() const;

    Type get_type() const { return type_; }
    bool get_boolean() const {
        Q_ASSERT(type_ == Type::Boolean);
        return b_;
    }

    bool operator== (const Value& other) const {
        if (type_ != other.type_) {
            return false;
        }
        switch (type_) {
        case Type::NoValue:     return true;
        case Type::Position:    return pos_ == other.pos_;
        case Type::Boolean:     return b_ == other.b_;
        }
    }
};

QDebug operator<< (QDebug d, const Value& v);

// A value which is desired by some plan, or which is currently present.
class Metric {
public:
    MetricType  type_;
    Value       arg_;   // optional argument for metric
    Value       value_;

    explicit Metric(MetricType ct): type_(ct) {}
    explicit Metric(MetricType ct, Value value)
        : type_(ct), value_(value) {}
    explicit Metric(MetricType ct, Value value, Value arg)
        : type_(ct), value_(value), arg_(arg) {}

    bool operator== (const Metric& other) const {
        Q_ASSERT(type_ == other.type_);
        Q_ASSERT(arg_ == other.arg_);
        return value_ == other.value_;
    }
};

QDebug operator<< (QDebug d, const Metric &m);

// TODO: a fixed-position array or a bitmap+metricvec combination maybe?
using MetricVec = std::vector<Metric>;

QDebug operator<< (QDebug d, const MetricVec &metrics);

// Context for metric (who is acting, and what is the target)
class Context {
public:
    const World* world_;
    const ComponentObject* actor_;
    Vec3i pos_;
    Context(const World *w, const ComponentObject *act, const Vec3i &p)
        : world_(w), actor_(act), pos_(p) {}
};

using MetricContextPair = std::pair<MetricVec, Context>;

}} // ns ai::bm
