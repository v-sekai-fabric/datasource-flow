// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Minimal JSON writer for the VRM (glTF) serializer. Append-only,
// streaming-style. Not a general-purpose JSON library — only handles
// the shapes the glTF emitter actually produces.
//
// Usage:
//   JsonWriter j;
//   j.begin_object();
//     j.key("asset"); j.begin_object();
//       j.key("version"); j.string("2.0");
//     j.end_object();
//     j.key("scenes"); j.begin_array();
//       j.begin_object();
//         j.key("nodes"); j.begin_array(); j.integer(0); j.end_array();
//       j.end_object();
//     j.end_array();
//   j.end_object();
//   std::string out = j.str();

#ifndef IDTX_CORE_INTERNAL_JSON_WRITER_H
#define IDTX_CORE_INTERNAL_JSON_WRITER_H

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace idtx::core {

class JsonWriter
{
public:
    JsonWriter();

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();

    void key(const char* name);
    void string(const char* value);
    void string(std::string const& value);
    void integer(int64_t value);
    void number(double value);
    void boolean(bool value);
    void null_value();

    // Convenience: write a contiguous array of primitives without
    // having to begin/end + iterate from the caller side.
    void int_array  (const int32_t* values, size_t count);
    void float_array(const float*   values, size_t count);

    std::string str() const;

private:
    void prefix_();
    void push_state_(char kind);  // 'O' object, 'A' array
    void pop_state_();

    std::ostringstream out_;
    std::vector<char>  stack_;
    std::vector<bool>  needs_comma_;   // top of stack = current scope
    bool               expecting_value_ = false;  // after a key()
};

}  // namespace idtx::core

#endif  // IDTX_CORE_INTERNAL_JSON_WRITER_H
