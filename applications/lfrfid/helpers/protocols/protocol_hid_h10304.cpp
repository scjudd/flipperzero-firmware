#include "protocol_hid_h10304.h"
#include <furi.h>

// http://www.proxmark.org/files/Documents/125%20kHz%20-%20HID/HID_format_example.pdf

// The hardware works with 32-bits at a time, making this a convenient unit
typedef uint32_t HID10304CardData;
constexpr uint8_t HID10304Count = 4;
constexpr uint8_t HID10304BitSize = sizeof(HID10304CardData) * 8;

static void write_raw_bit(bool bit, uint8_t position, HID10304CardData *card_data) {
    if(bit) {
        card_data[position / HID10304BitSize ] |=
            1UL << (HID10304BitSize - (position % HID10304BitSize) - 1);
    } else {
        card_data[position / (sizeof(HID10304CardData) * 8)] &=
            ~(1UL << (HID10304BitSize - (position % HID10304BitSize) - 1));
    }
}

static void write_manchester_bit(bool bit, uint8_t position, HID10304CardData *card_data) {
    // Manchester encoding: every data bit is encoded as a transition from high-to-low or low-to-high
    write_raw_bit(bit, position + 0, card_data);
    write_raw_bit(!bit, position + 1, card_data);
}

uint8_t ProtocolHID10304::get_encoded_data_size() {
    return sizeof(HID10304CardData) * HID10304Count;
}

uint8_t ProtocolHID10304::get_decoded_data_size() {
    return 5; // number of bytes to hold payload: just over 4.
}

void ProtocolHID10304::encode(
    const uint8_t* decoded_data,
    const uint8_t decoded_data_size,
    uint8_t* encoded_data,
    const uint8_t encoded_data_size) {
    furi_check(decoded_data_size >= get_decoded_data_size());
    furi_check(encoded_data_size >= get_encoded_data_size());

    HID10304CardData card_data[HID10304Count] = {0, 0, 0, 0};

    // faculty code, card number -- no parity
    uint64_t fc_cn = (
        ((uint64_t)decoded_data[0] << 24)
        | ((uint64_t)decoded_data[1] << 16)
        | ((uint64_t)decoded_data[2] << 8)
        | (uint64_t)decoded_data[3]
    );

    //  H10304:   0123456789012345678901234
    //  01234567891111111111222222222233333
    // PFFFFFFFFFFFFFFFFCCCCCCCCCCCCCCCCCCCP
    // EXXXXXXXXXXXXXXXXXX..................
    // ..................XXXXXXXXXXXXXXXXXXO
    //
    //  H10301:   01234567890123
    //  012345678911111111112222
    // PFFFFFFFFCCCCCCCCCCCCCCCCP
    // EXXXXXXXXXXXX.............
    // .............XXXXXXXXXXXXO

    // even parity sum calculation (high 19 bits of data)
    uint8_t even_parity_sum = 0;
    for(int8_t i = 17; i < 35; i++) {
        if(((fc_cn >> i) & 1) == 1) {
            even_parity_sum++;
        }
    }

    // odd parity sum calculation (low 19 bits of data)
    uint8_t odd_parity_sum = 1;
    for(int8_t i = 0; i < 18; i++) {
        if(((fc_cn >> i) & 1) == 1) {
            odd_parity_sum++;
        }
    }

    // 0x1D preamble
    write_raw_bit(0, 10 + 0, card_data);
    write_raw_bit(0, 10 + 1, card_data);
    write_raw_bit(0, 10 + 2, card_data);
    write_raw_bit(1, 10 + 3, card_data);
    write_raw_bit(1, 10 + 4, card_data);
    write_raw_bit(1, 10 + 5, card_data);
    write_raw_bit(0, 10 + 6, card_data);
    write_raw_bit(1, 10 + 7, card_data);

    // company / OEM code 1
    write_manchester_bit(0, 10 + 8, card_data);
    write_manchester_bit(0, 10 + 10, card_data);
    write_manchester_bit(0, 10 + 12, card_data);
    write_manchester_bit(0, 10 + 14, card_data);
    write_manchester_bit(0, 10 + 16, card_data);
    write_manchester_bit(0, 10 + 18, card_data);
    write_manchester_bit(1, 10 + 20, card_data);

    // card format / length 4 (guessing here, since it's H10304 vs H10301)
    write_manchester_bit(0, 10 + 22, card_data);
    write_manchester_bit(0, 10 + 24, card_data);
    write_manchester_bit(0, 10 + 26, card_data);
    write_manchester_bit(0, 10 + 28, card_data);
    write_manchester_bit(0, 10 + 30, card_data);
    write_manchester_bit(0, 10 + 32, card_data);
    write_manchester_bit(0, 10 + 34, card_data);
    write_manchester_bit(0, 10 + 36, card_data);
    write_manchester_bit(1, 10 + 38, card_data);
    write_manchester_bit(0, 10 + 40, card_data);
    write_manchester_bit(0, 10 + 42, card_data);

    // even parity bit
    write_manchester_bit((even_parity_sum % 2), 10 + 44, card_data);

    // data
    for (uint8_t i = 0; i < 35; i++) {
        write_manchester_bit((fc_cn >> (34 - i) & 1), 10 + 46 + (i * 2), card_data);
    }

    // odd parity bit
    write_manchester_bit((odd_parity_sum % 2), 10 + 118, card_data);

    memcpy(encoded_data, &card_data, get_encoded_data_size());
}

void ProtocolHID10304::decode(
    const uint8_t* encoded_data,
    const uint8_t encoded_data_size,
    uint8_t* decoded_data,
    const uint8_t decoded_data_size) {
    furi_check(decoded_data_size >= get_decoded_data_size());
    furi_check(encoded_data_size >= get_encoded_data_size());

    const HID10304CardData* card_data = reinterpret_cast<const HID10304CardData*>(encoded_data);

    // data decoding
    uint64_t result = 0;

    // decode from word 1
    // coded with 01 = 0, 10 = 1 transitions
    for(int8_t i = 9; i >= 0; i--) {
        switch((*(card_data + 1) >> (2 * i)) & 0b11) {
            case 0b01:
                result = (result << 1) | 0;
                break;
            case 0b10:
                result = (result << 1) | 1;
                break;
            default:
                break;
        }
    }

    // decode from word 2
    // coded with 01 = 0, 10 = 1 transitions
    for(int8_t i = 15; i >= 0; i--) {
        switch((*(card_data + 2) >> (2 * i)) & 0b11) {
            case 0b01:
                result = (result << 1) | 0;
                break;
            case 0b10:
                result = (result << 1) | 1;
                break;
            default:
                break;
        }
    }

    // decode from word 3
    // coded with 01 = 0, 10 = 1 transitions
    for(int8_t i = 15; i >= 0; i--) {
        switch((*(card_data + 3) >> (2 * i)) & 0b11) {
            case 0b01:
                result = (result << 1) | 0;
                break;
            case 0b10:
                result = (result << 1) | 1;
                break;
            default:
                break;
        }
    }

    uint8_t data[5] = {
        (uint8_t)(result >> 33),
        (uint8_t)(result >> 25),
        (uint8_t)(result >> 17),
        (uint8_t)(result >> 9),
        (uint8_t)(result >> 1),
    };

    printf("decoded: %02X %02X %02X %02X %02X\r\n",
        data[0], data[1], data[2], data[3], data[4]);

    memcpy(decoded_data, &data, get_decoded_data_size());
}

bool ProtocolHID10304::can_be_decoded(const uint8_t* encoded_data, const uint8_t encoded_data_size) {
    furi_check(encoded_data_size >= get_encoded_data_size());

    const HID10304CardData* card_data = reinterpret_cast<const HID10304CardData*>(encoded_data);

    // packet preamble
    if((*card_data >> 7 & 0xFF) != 0x1D) {
        return false;
    }

    // encoded company/oem
    // coded with 01 = 0, 10 = 1 transitions
    // stored in word 0
    // if((*card_data >> 10 & 0x3FFF) != 0x1556) {
    //     return false;
    // }
    return (*card_data & 0x3FFF) == 0x1556;

    /*
    // encoded format/length
    // coded with 01 = 0, 10 = 1 transitions
    // stored in word 0 and word 1
    if((((*card_data & 0x3FF) << 12) | ((*(card_data + 1) >> 20) & 0xFFF)) != 0x155556) {
        return false;
    }

    // data decoding
    uint32_t result = 0;

    // decode from word 1
    // coded with 01 = 0, 10 = 1 transitions
    for(int8_t i = 9; i >= 0; i--) {
        switch((*(card_data + 1) >> (2 * i)) & 0b11) {
        case 0b01:
            result = (result << 1) | 0;
            break;
        case 0b10:
            result = (result << 1) | 1;
            break;
        default:
            return false;
            break;
        }
    }

    // decode from word 2
    // coded with 01 = 0, 10 = 1 transitions
    for(int8_t i = 15; i >= 0; i--) {
        switch((*(card_data + 2) >> (2 * i)) & 0b11) {
        case 0b01:
            result = (result << 1) | 0;
            break;
        case 0b10:
            result = (result << 1) | 1;
            break;
        default:
            return false;
            break;
        }
    }

    // trailing parity (odd) test
    uint8_t parity_sum = 0;
    for(int8_t i = 0; i < 13; i++) {
        if(((result >> i) & 1) == 1) {
            parity_sum++;
        }
    }

    if((parity_sum % 2) != 1) {
        return false;
    }

    // leading parity (even) test
    parity_sum = 0;
    for(int8_t i = 13; i < 26; i++) {
        if(((result >> i) & 1) == 1) {
            parity_sum++;
        }
    }

    if((parity_sum % 2) == 1) {
        return false;
    }

    return true;
    */
}
