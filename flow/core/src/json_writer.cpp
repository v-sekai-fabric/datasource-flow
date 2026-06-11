// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "idtx_core/internal/json_writer.h"

#include <cctype>

namespace idtx::core {

JsonWriter::JsonWriter() = default;

void JsonWriter::push_state_(char kind)
{
    stack_.push_back(kind);
    needs_comma_.push_back(false);
}

void JsonWriter::pop_state_()
{
    if (!stack_.empty()) {
        stack_.pop_back();
        needs_comma_.pop_back();
    }
}

void JsonWriter::prefix_()
{
    if (expecting_value_) {
        // We just emitted a key + ':', the value follows immediately.
        expecting_value_ = false;
        return;
    }
    if (!stack_.empty()) {
        if (needs_comma_.back()) out_ << ',';
        needs_comma_.back() = true;
    }
}

void JsonWriter::begin_object()
{
    prefix_();
    out_ << '{';
    push_state_('O');
}

void JsonWriter::end_object()
{
    out_ << '}';
    pop_state_();
}

void JsonWriter::begin_array()
{
    prefix_();
    out_ << '[';
    push_state_('A');
}

void JsonWriter::end_array()
{
    out_ << ']';
    pop_state_();
}

static void escape_string(std::ostringstream& o, const char* s)
{
    o << '"';
    for (; *s != '\0'; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o << buf;
                } else {
                    o << static_cast<char>(c);
                }
        }
    }
    o << '"';
}

void JsonWriter::key(const char* name)
{
    prefix_();
    escape_string(out_, name);
    out_ << ':';
    expecting_value_ = true;
}

void JsonWriter::string(const char* value)
{
    prefix_();
    escape_string(out_, value != nullptr ? value : "");
}

void JsonWriter::string(std::string const& value)
{
    string(value.c_str());
}

void JsonWriter::integer(int64_t value)
{
    prefix_();
    out_ << value;
}

void JsonWriter::number(double value)
{
    prefix_();
    // Avoid locale issues + write JSON-compatible doubles.
    if (value != value) { out_ << "null"; return; }   // NaN -> null per glTF convention
    out_.precision(9);
    out_ << value;
}

void JsonWriter::boolean(bool value)
{
    prefix_();
    out_ << (value ? "true" : "false");
}

void JsonWriter::null_value()
{
    prefix_();
    out_ << "null";
}

void JsonWriter::int_array(const int32_t* values, size_t count)
{
    begin_array();
    for (size_t i = 0; i < count; ++i) integer(values[i]);
    end_array();
}

void JsonWriter::float_array(const float* values, size_t count)
{
    begin_array();
    for (size_t i = 0; i < count; ++i) number(static_cast<double>(values[i]));
    end_array();
}

std::string JsonWriter::str() const
{
    return out_.str();
}

}  // namespace idtx::core
