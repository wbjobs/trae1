#include "protocol.h"
#include <cstring>
#include <stdexcept>

namespace sgxagg {

uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8)  & 0xff;
    p[3] =  v        & 0xff;
}

std::vector<uint8_t> serialize_records(const std::vector<Record>& records) {
    std::vector<uint8_t> out(sizeof(uint32_t) + records.size() * (sizeof(double) + sizeof(int32_t)));
    write_be32(out.data(), (uint32_t)records.size());
    size_t off = 4;
    for (const auto& r : records) {
        std::memcpy(out.data() + off, &r.income, sizeof(double));
        off += sizeof(double);
        std::memcpy(out.data() + off, &r.age, sizeof(int32_t));
        off += sizeof(int32_t);
    }
    out.resize(off);
    return out;
}

std::vector<Record> deserialize_records(const uint8_t* data, size_t len) {
    if (len < 4) throw std::runtime_error("records payload too short");
    uint32_t n = read_be32(data);
    size_t expected = 4 + (size_t)n * (sizeof(double) + sizeof(int32_t));
    if (len < expected) throw std::runtime_error("records payload truncated");
    std::vector<Record> out(n);
    size_t off = 4;
    for (uint32_t i = 0; i < n; ++i) {
        std::memcpy(&out[i].income, data + off, sizeof(double));
        off += sizeof(double);
        std::memcpy(&out[i].age, data + off, sizeof(int32_t));
        off += sizeof(int32_t);
    }
    return out;
}

std::vector<uint8_t> serialize_aggregate(const AggregateResult& r) {
    std::vector<uint8_t> out(sizeof(AggregateResult));
    std::memcpy(out.data(), &r, sizeof(AggregateResult));
    return out;
}

AggregateResult deserialize_aggregate(const uint8_t* data, size_t len) {
    if (len < sizeof(AggregateResult)) throw std::runtime_error("aggregate payload too short");
    AggregateResult r;
    std::memcpy(&r, data, sizeof(AggregateResult));
    return r;
}

// —— PIR 查询条件构建 ——
std::vector<uint8_t> make_leaf_condition(QueryField field, QueryOp op, double value) {
    std::vector<uint8_t> out(11);
    out[0] = 0;  // 叶子节点标识
    out[1] = (uint8_t)field;
    out[2] = (uint8_t)op;
    std::memcpy(out.data() + 3, &value, sizeof(double));
    return out;
}

std::vector<uint8_t> make_composite_condition(QueryOp op,
    const std::vector<uint8_t>& left, const std::vector<uint8_t>& right) {
    std::vector<uint8_t> out;
    out.reserve(2 + left.size() + right.size());
    out.push_back(1);  // 内部节点标识
    out.push_back((uint8_t)op);
    out.insert(out.end(), left.begin(), left.end());
    out.insert(out.end(), right.begin(), right.end());
    return out;
}

std::vector<uint8_t> serialize_pir_query(const PIRQuery& q, const uint8_t* condition) {
    std::vector<uint8_t> out(6 + q.condition_len);
    out[0] = (uint8_t)q.type;
    out[1] = (uint8_t)q.target_field;
    write_be32(out.data() + 2, q.condition_len);
    if (q.condition_len > 0 && condition) {
        std::memcpy(out.data() + 6, condition, q.condition_len);
    }
    return out;
}

bool deserialize_pir_query(const uint8_t* data, size_t len, PIRQuery& q,
    const uint8_t*& out_condition, size_t& out_condition_len) {
    if (len < 6) return false;
    q.type = (QueryType)data[0];
    q.target_field = (QueryField)data[1];
    q.condition_len = read_be32(data + 2);
    if (len < 6 + q.condition_len) return false;
    out_condition = data + 6;
    out_condition_len = q.condition_len;
    return true;
}

std::vector<uint8_t> serialize_pir_result(const PIRResult& r) {
    std::vector<uint8_t> out(sizeof(PIRResult));
    std::memcpy(out.data(), &r, sizeof(PIRResult));
    return out;
}

PIRResult deserialize_pir_result(const uint8_t* data, size_t len) {
    if (len < sizeof(PIRResult)) throw std::runtime_error("pir result payload too short");
    PIRResult r;
    std::memcpy(&r, data, sizeof(PIRResult));
    return r;
}

// 递归评估条件 AST
static bool eval_cond(const uint8_t*& p, const Record& r, int depth) {
    if (depth > 8) return false;  // 防止恶意深度攻击
    if (*p == 0) {
        // 叶子节点
        QueryField f = (QueryField)p[1];
        QueryOp op = (QueryOp)p[2];
        double val;
        std::memcpy(&val, p + 3, sizeof(double));
        p += 11;

        double field_val = (f == QueryField::Income) ? r.income : (double)r.age;
        switch (op) {
            case QueryOp::Eq: return field_val == val;
            case QueryOp::Ne: return field_val != val;
            case QueryOp::Gt: return field_val > val;
            case QueryOp::Ge: return field_val >= val;
            case QueryOp::Lt: return field_val < val;
            case QueryOp::Le: return field_val <= val;
            default: return false;
        }
    } else {
        // 内部节点
        QueryOp op = (QueryOp)p[1];
        p += 2;
        bool left = eval_cond(p, r, depth + 1);
        bool right = eval_cond(p, r, depth + 1);
        if (op == QueryOp::And) return left && right;
        if (op == QueryOp::Or)  return left || right;
        return false;
    }
}

bool evaluate_condition(const uint8_t* cond, size_t cond_len, const Record& r) {
    if (cond_len < 2) return false;
    const uint8_t* p = cond;
    return eval_cond(p, r, 0);
}

} // namespace sgxagg
