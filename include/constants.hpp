#pragma once

#include <cstdint>

static constexpr std::uint32_t G923_VENDOR_ID = 0x046D;
static constexpr std::uint32_t G923_PRODUCT_ID = 0xC266;
static constexpr std::uint32_t G923_DEVICE_ID = 0xC266046D;

static constexpr std::size_t COMMAND_MAX_LENGTH = 8;
static constexpr std::size_t COMMAND_MAX_COUNT = 4;

static constexpr int FORCE_UPDATE_RATE = 8;  // Force feedback update every 8 frames
static constexpr int LED_UPDATE_RATE = 32;   // LED update every 32 frames

static constexpr std::uint8_t LED_PATTERN_OFF = 0x00;
static constexpr std::uint8_t LED_PATTERN_1 = 0x01;
static constexpr std::uint8_t LED_PATTERN_2 = 0x03;
static constexpr std::uint8_t LED_PATTERN_3 = 0x07;
static constexpr std::uint8_t LED_PATTERN_4 = 0x0F;
static constexpr std::uint8_t LED_PATTERN_5 = 0x1F;

static constexpr const char* VERSION = "0.1.0";
