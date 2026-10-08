/*
 * Copyright (C) 2021, 2024 nukeykt
 *
 *  Redistribution and use of this code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *   - Redistributions may not be sold, nor may they be used in a commercial
 *     product or activity.
 *
 *   - Redistributions that are modified from the original source must include the
 *     complete source code, including the source code for all components used by a
 *     binary built from the modified sources. However, as a special exception, the
 *     source code distributed need not include anything that is normally distributed
 *     (in either source or binary form) with the major components (compiler, kernel,
 *     and so on) of the operating system on which the executable runs, unless that
 *     component itself accompanies the executable.
 *
 *   - Redistributions must reproduce the above copyright notice, this list of
 *     conditions and the following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>

struct mcu_t;

struct PCM_Config
{
    // config_reg_3c
    uint32_t orval        = 0;
    int      dac_mask     = 0; // unused
    uint8_t  noise_mask   = 0;
    uint8_t  write_mask   = 0;
    bool     oversampling = false;

    // config_reg_3d
    // important that this starts at 1, see derivation in PCM_Write
    uint8_t reg_slots = 1;
};

// Zero-filled block for all wave ROMs; calloc leaves untouched pages unbacked.
inline std::shared_ptr<uint8_t> pcm_alloc_waverom_block()
{
    constexpr size_t total = 0x200000 + 0x200000 + 0x100000 + 0x200000 + 0x800000;
    return std::shared_ptr<uint8_t>(static_cast<uint8_t*>(std::calloc(total, 1)), [](uint8_t* p) { std::free(p); });
}

struct pcm_t
{
    uint32_t ram1[32][8]{};
    uint16_t ram2[32][16]{};
    mcu_t*   mcu                 = nullptr;
    uint64_t cycles              = 0;
    uint32_t voice_mask          = 0; // same size as voice_mask_pending
    uint32_t voice_mask_pending  = 0; // 28 bits wide?
    uint32_t write_latch         = 0; // 20 bits wide?
    uint32_t read_latch          = 0; // 20 bits wide?
    uint32_t wave_read_address   = 0;
    uint16_t tv_counter          = 0; // 14 bits wide?
    uint8_t  wave_byte_latch     = 0;
    uint8_t  select_channel      = 0; // 5 bits wide?
    uint8_t  config_reg_3c       = 0; // SC55:c3 JV880:c0
    uint8_t  config_reg_3d       = 0;
    uint8_t  irq_channel         = 0; // range 1..32
    bool     irq_assert          = 0;
    bool     voice_mask_updating = false;
    bool     nfs                 = false;
    int32_t  accum_l             = 0;
    int32_t  accum_r             = 0;
    int32_t  rcsum[2]{};

    PCM_Config config{};

    uint16_t eram[0x4000]{};

    // Wave ROMs (read-only after loading). They live in one block that is
    // allocated zero-filled without touching it (unused areas such as the card
    // or JV-880 expansion ROM therefore occupy no physical memory) and can be
    // shared between several emulators running the same ROM set
    // (Nuked SC-55 Poly: one copy for all units). Access is unchanged:
    // pcm.waverom1[address] etc.
    static constexpr size_t WAVEROM1_SIZE = 0x200000, WAVEROM2_SIZE = 0x200000, WAVEROM3_SIZE = 0x100000,
                             WAVEROM_CARD_SIZE = 0x200000, WAVEROM_EXP_SIZE = 0x800000;
    std::shared_ptr<uint8_t> waverom_block = pcm_alloc_waverom_block();
    uint8_t* waverom1     = waverom_block.get();
    uint8_t* waverom2     = waverom1 + WAVEROM1_SIZE;
    uint8_t* waverom3     = waverom2 + WAVEROM2_SIZE;
    uint8_t* waverom_card = waverom3 + WAVEROM3_SIZE;
    uint8_t* waverom_exp  = waverom_card + WAVEROM_CARD_SIZE;

    // Use the (already loaded) wave ROMs of another emulator; frees our own block.
    void share_waveroms_from(const pcm_t& other)
    {
        waverom_block = other.waverom_block;
        waverom1      = waverom_block.get();
        waverom2      = waverom1 + WAVEROM1_SIZE;
        waverom3      = waverom2 + WAVEROM2_SIZE;
        waverom_card  = waverom3 + WAVEROM3_SIZE;
        waverom_exp   = waverom_card + WAVEROM_CARD_SIZE;
    }

    bool enable_oversampling = true;
};

void PCM_Write(pcm_t& pcm, uint32_t address, uint8_t data);
uint8_t PCM_Read(pcm_t& pcm, uint32_t address);
void PCM_Init(pcm_t& pcm, mcu_t& mcu);
void PCM_Update(pcm_t& pcm, uint64_t cycles);
uint32_t PCM_GetOutputFrequency(const pcm_t& pcm);
void PCM_GetConfig(PCM_Config& config, uint8_t config_byte);
