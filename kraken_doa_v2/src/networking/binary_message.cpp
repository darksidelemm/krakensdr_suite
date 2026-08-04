#include "networking/binary_message.hpp"

void BinaryMessage::clear() { 
    data.clear(); 
}

void BinaryMessage::add_array(const void* array, size_t size) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(array);
    data.insert(data.end(), bytes, bytes + size);
}

std::string BinaryMessage::to_string() const {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

size_t BinaryMessage::size() const { 
    return data.size(); 
}

const uint8_t* BinaryMessage::get_data() const { 
    return data.data(); 
}
