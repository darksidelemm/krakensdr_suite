#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

class BinaryMessage {
private:
    std::vector<uint8_t> data;
    
public:
    void clear();
    
    template<typename T>
    void add_value(const T& value) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        data.insert(data.end(), bytes, bytes + sizeof(T));
    }
    
    void add_array(const void* array, size_t size);
    
    template<typename T>
    void add_vector(const std::vector<T>& vec) {
        add_array(vec.data(), vec.size() * sizeof(T));
    }
    
    std::string to_string() const;
    size_t size() const;
    const uint8_t* get_data() const;
};
