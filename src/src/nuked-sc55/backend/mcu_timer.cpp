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

#include "mcu_timer.h"
#include "mcu.h"
#include <bit>
#include <cstdint>

enum TMR_TCR_Bits : uint8_t
{
    TMR_TCR_CKS0  = 1 << 0, // Clock Select 0
    TMR_TCR_CKS1  = 1 << 1, // Clock Select 1
    TMR_TCR_CKS2  = 1 << 2, // Clock Select 2
    TMR_TCR_CCLR0 = 1 << 3, // Counter Clear 0
    TMR_TCR_CCLR1 = 1 << 4, // Counter Clear 1
    TMR_TCR_OVIE  = 1 << 5, // Timer Overflow Interrupt Enable
    TMR_TCR_CMIEA = 1 << 6, // Compare-match Interrupt Enable A
    TMR_TCR_CMIEB = 1 << 7, // Compare-match Interrupt Enable B
};

enum TMR_TCSR_Bits : uint8_t
{
    TMR_TCSR_OS0  = 1 << 0, // Output Select 0
    TMR_TCSR_OS1  = 1 << 1, // Output Select 1
    TMR_TCSR_OS2  = 1 << 2, // Output Select 2
    TMR_TCSR_OS3  = 1 << 3, // Output Select 3
    TMR_TCSR_BIT4 = 1 << 4, // Reserved
    TMR_TCSR_OVF  = 1 << 5, // Timer Overflow Flag
    TMR_TCSR_CMFA = 1 << 6, // Compare-Match Flag A
    TMR_TCSR_CMFB = 1 << 7, // Compare-Match Flag B
};

enum FRT_TCR_Bits : uint8_t
{
    FRT_TCR_CKS0  = 1 << 0, // Clock Select 0
    FRT_TCR_CKS1  = 1 << 1, // Clock Select 1
    FRT_TCR_OEA   = 1 << 2, // Output Enable A
    FRT_TCR_OEB   = 1 << 3, // Output Enable B
    FRT_TCR_OVIE  = 1 << 4, // Timer overflow Interrupt Enable
    FRT_TCR_OCIEA = 1 << 5, // Output Compare Interrupt Enable A
    FRT_TCR_OCIEB = 1 << 6, // Output Compare Interrupt Enable B
    FRT_TCR_ICIE  = 1 << 7, // Input Capture Interrupt Enable
};

enum FRT_TCSR_Bits : uint8_t
{
    FRT_TCSR_CCLRA = 1 << 0, // Counter Clear A
    FRT_TCSR_IEDG  = 1 << 1, // Input Edge Select
    FRT_TCSR_OLVLA = 1 << 2, // Output Level A
    FRT_TCSR_OLVLB = 1 << 3, // Output Level B
    FRT_TCSR_OVF   = 1 << 4, // Timer Overflow Flag
    FRT_TCSR_OCFA  = 1 << 5, // Output Compare Flag A
    FRT_TCSR_OCFB  = 1 << 6, // Output Compare Flag B
    FRT_TCSR_ICF   = 1 << 7, // Input Capture Flag
};

// Values are byte offsets from start of FRTs in memory (ffa0, ffb0, ffc0)
enum FRT_Field_Offset : uint8_t
{
    REG_TCR   = 0x00,
    REG_TCSR  = 0x01,
    REG_FRCH  = 0x02,
    REG_FRCL  = 0x03,
    REG_OCRAH = 0x04,
    REG_OCRAL = 0x05,
    REG_OCRBH = 0x06,
    REG_OCRBL = 0x07,
    REG_ICRH  = 0x08,
    REG_ICRL  = 0x09,
};

static void TIMER_Sync(mcu_timer_t& timer);

void TIMER_Init(mcu_timer_t& timer, mcu_t& mcu)
{
    timer.mcu = &mcu;
}

void TIMER_Reset(mcu_timer_t& timer)
{
    TIMER_Sync(timer);
    for (bool& d : timer.frt_dirty) d = true;
    timer.tmr_dirty  = true;
    timer.next_event = 0;

    for (int i = 0; i < 3; ++i)
    {
        timer.frt[i] = {
            .tcr       = 0,
            .tcsr      = 0,
            .frc       = 0,
            .ocra      = 0xffff,
            .ocrb      = 0xffff,
            .icr       = 0,
            .status_rd = 0,
        };
    }
    timer.tmr = {
        .tcr       = 0,
        .tcsr      = TMR_TCSR_BIT4,
        .tcora     = 0xff,
        .tcorb     = 0xff,
        .tcnt      = 0,
        .status_rd = 0,
    };
}

void TIMER_Write(mcu_timer_t& timer, uint32_t address, uint8_t data)
{
    TIMER_Sync(timer);
    timer.next_event = 0;
    uint32_t t = (address >> 4) - 1;
    if (t > 2)
        return;
    frt_t& frt = timer.frt[t];
    timer.frt_dirty[t] = true;

    address &= 0x0f;
    switch (address)
    {
    case REG_TCR:
        frt.tcr = data;
        break;
    case REG_TCSR:
        frt.tcsr &= ~0xf;
        frt.tcsr |= data & 0xf;
        if ((data & FRT_TCSR_OVF) == 0 && (frt.status_rd & FRT_TCSR_OVF) != 0)
        {
            frt.tcsr      &= ~FRT_TCSR_OVF;
            frt.status_rd &= ~FRT_TCSR_OVF;
            MCU_Interrupt_SetRequest(*timer.mcu, (MCU_Interrupt_Source)(INTERRUPT_SOURCE_FRT0_FOVI + t * 4), 0);
        }
        if ((data & FRT_TCSR_OCFA) == 0 && (frt.status_rd & FRT_TCSR_OCFA) != 0)
        {
            frt.tcsr      &= ~FRT_TCSR_OCFA;
            frt.status_rd &= ~FRT_TCSR_OCFA;
            MCU_Interrupt_SetRequest(*timer.mcu, (MCU_Interrupt_Source)(INTERRUPT_SOURCE_FRT0_OCIA + t * 4), 0);
        }
        if ((data & FRT_TCSR_OCFB) == 0 && (frt.status_rd & FRT_TCSR_OCFB) != 0)
        {
            frt.tcsr      &= ~FRT_TCSR_OCFB;
            frt.status_rd &= ~FRT_TCSR_OCFB;
            MCU_Interrupt_SetRequest(*timer.mcu, (MCU_Interrupt_Source)(INTERRUPT_SOURCE_FRT0_OCIB + t * 4), 0);
        }
        break;
    case REG_FRCH:
    case REG_OCRAH:
    case REG_OCRBH:
    case REG_ICRH:
        timer.tempreg = data;
        break;
    case REG_FRCL:
        frt.frc = (uint16_t)((timer.tempreg << 8) | data);
        break;
    case REG_OCRAL:
        frt.ocra = (uint16_t)((timer.tempreg << 8) | data);
        break;
    case REG_OCRBL:
        frt.ocrb = (uint16_t)((timer.tempreg << 8) | data);
        break;
    case REG_ICRL:
        frt.icr = (uint16_t)((timer.tempreg << 8) | data);
        break;
    }
}

uint8_t TIMER_Read(mcu_timer_t& timer, uint32_t address)
{
    TIMER_Sync(timer);
    uint32_t t = (address >> 4) - 1;
    if (t > 2)
        return 0xff;
    frt_t& frt = timer.frt[t];

    address &= 0x0f;
    switch (address)
    {
    case REG_TCR:
        return frt.tcr;
    case REG_TCSR: {
        uint8_t ret    = frt.tcsr;
        frt.status_rd |= frt.tcsr & 0xf0;
        // frt.status_rd |= 0xf0;
        return ret;
    }
    case REG_FRCH:
        timer.tempreg = (uint8_t)frt.frc;
        return (uint8_t)(frt.frc >> 8);
    case REG_OCRAH:
        timer.tempreg = (uint8_t)frt.ocra;
        return (uint8_t)(frt.ocra >> 8);
    case REG_OCRBH:
        timer.tempreg = (uint8_t)frt.ocrb;
        return (uint8_t)(frt.ocrb >> 8);
    case REG_ICRH:
        timer.tempreg = (uint8_t)frt.icr;
        return (uint8_t)(frt.icr >> 8);
    case REG_FRCL:
    case REG_OCRAL:
    case REG_OCRBL:
    case REG_ICRL:
        return timer.tempreg;
    }
    return 0xff;
}

void TIMER2_Write(mcu_timer_t& timer, uint32_t address, uint8_t data)
{
    TIMER_Sync(timer);
    timer.next_event = 0;
    tmr_t& tmr = timer.tmr;
    timer.tmr_dirty = true;

    switch (address)
    {
    case DEV_TMR_TCR:
        tmr.tcr = data;
        break;
    case DEV_TMR_TCSR:
        tmr.tcsr &= ~0xf;
        tmr.tcsr |= data & 0xf;
        if ((data & TMR_TCSR_OVF) == 0 && (tmr.status_rd & TMR_TCSR_OVF) != 0)
        {
            tmr.tcsr      &= ~TMR_TCSR_OVF;
            tmr.status_rd &= ~TMR_TCSR_OVF;
            MCU_Interrupt_SetRequest(*timer.mcu, INTERRUPT_SOURCE_TIMER_OVI, 0);
        }
        if ((data & TMR_TCSR_CMFA) == 0 && (tmr.status_rd & TMR_TCSR_CMFA) != 0)
        {
            tmr.tcsr      &= ~TMR_TCSR_CMFA;
            tmr.status_rd &= ~TMR_TCSR_CMFA;
            MCU_Interrupt_SetRequest(*timer.mcu, INTERRUPT_SOURCE_TIMER_CMIA, 0);
        }
        if ((data & TMR_TCSR_CMFB) == 0 && (tmr.status_rd & TMR_TCSR_CMFB) != 0)
        {
            tmr.tcsr      &= ~TMR_TCSR_CMFB;
            tmr.status_rd &= ~TMR_TCSR_CMFB;
            MCU_Interrupt_SetRequest(*timer.mcu, INTERRUPT_SOURCE_TIMER_CMIB, 0);
        }
        break;
    case DEV_TMR_TCORA:
        tmr.tcora = data;
        break;
    case DEV_TMR_TCORB:
        tmr.tcorb = data;
        break;
    case DEV_TMR_TCNT:
        tmr.tcnt = data;
        break;
    }
}

uint8_t TIMER_Read2(mcu_timer_t& timer, uint32_t address)
{
    TIMER_Sync(timer);
    tmr_t& tmr = timer.tmr;

    switch (address)
    {
    case DEV_TMR_TCR:
        return tmr.tcr;
    case DEV_TMR_TCSR: {
        uint8_t ret    = tmr.tcsr;
        tmr.status_rd |= tmr.tcsr & (TMR_TCSR_OVF | TMR_TCSR_CMFA | TMR_TCSR_CMFB);
        return ret;
    }
    case DEV_TMR_TCORA:
        return tmr.tcora;
    case DEV_TMR_TCORB:
        return tmr.tcorb;
    case DEV_TMR_TCNT:
        return tmr.tcnt;
    }
    return 0xff;
}

// One complete FRT tick (original logic, without the clock gate).
static inline void TIMER_TickFrt(mcu_timer_t& timer, int frt_id)
{
    frt_t& frt = timer.frt[frt_id];

    const bool matcha = frt.frc == frt.ocra;
    const bool matchb = frt.frc == frt.ocrb;
    if ((frt.tcsr & FRT_TCSR_CCLRA) && matcha) // CCLRA
    {
        frt.frc = 0;
    }
    else
    {
        ++frt.frc;
        if (frt.frc == 0)
        {
            frt.tcsr |= FRT_TCSR_OVF;
        }
    }

    // flags
    if (matcha)
        frt.tcsr |= FRT_TCSR_OCFA;
    if (matchb)
        frt.tcsr |= FRT_TCSR_OCFB;

    if ((frt.tcr & FRT_TCR_OVIE) != 0 && (frt.tcsr & FRT_TCSR_OVF) != 0)
        MCU_Interrupt_SetRequest(*timer.mcu, (MCU_Interrupt_Source)(INTERRUPT_SOURCE_FRT0_FOVI + frt_id * 4), 1);
    if ((frt.tcr & FRT_TCR_OCIEA) != 0 && (frt.tcsr & FRT_TCSR_OCFA) != 0)
        MCU_Interrupt_SetRequest(*timer.mcu, (MCU_Interrupt_Source)(INTERRUPT_SOURCE_FRT0_OCIA + frt_id * 4), 1);
    if ((frt.tcr & FRT_TCR_OCIEB) != 0 && (frt.tcsr & FRT_TCSR_OCFB) != 0)
        MCU_Interrupt_SetRequest(*timer.mcu, (MCU_Interrupt_Source)(INTERRUPT_SOURCE_FRT0_OCIB + frt_id * 4), 1);
}

// One complete 8-bit timer tick (original logic, without the clock gate).
static inline void TIMER_TickTmr(mcu_timer_t& timer)
{
    tmr_t& tmr = timer.tmr;

    const bool matcha = tmr.tcnt == tmr.tcora;
    const bool matchb = tmr.tcnt == tmr.tcorb;
    if ((tmr.tcr & (TMR_TCR_CCLR0 | TMR_TCR_CCLR1)) == TMR_TCR_CCLR0 && matcha)
    {
        tmr.tcnt = 0;
    }
    else if ((tmr.tcr & (TMR_TCR_CCLR0 | TMR_TCR_CCLR1)) == TMR_TCR_CCLR1 && matchb)
    {
        tmr.tcnt = 0;
    }
    else
    {
        ++tmr.tcnt;
        if (tmr.tcnt == 0)
        {
            tmr.tcsr |= TMR_TCSR_OVF;
        }
    }

    // flags
    if (matcha)
        tmr.tcsr |= TMR_TCSR_CMFA;
    if (matchb)
        tmr.tcsr |= TMR_TCSR_CMFB;

    if ((tmr.tcr & TMR_TCR_OVIE) != 0 && (tmr.tcsr & TMR_TCSR_OVF) != 0)
        MCU_Interrupt_SetRequest(*timer.mcu, INTERRUPT_SOURCE_TIMER_OVI, 1);
    if ((tmr.tcr & TMR_TCR_CMIEA) != 0 && (tmr.tcsr & TMR_TCSR_CMFA) != 0)
        MCU_Interrupt_SetRequest(*timer.mcu, INTERRUPT_SOURCE_TIMER_CMIA, 1);
    if ((tmr.tcr & TMR_TCR_CMIEB) != 0 && (tmr.tcsr & TMR_TCSR_CMFB) != 0)
        MCU_Interrupt_SetRequest(*timer.mcu, INTERRUPT_SOURCE_TIMER_CMIB, 1);
}

// Number of timer cycles c in [t0, t1) with (c & mask) == 0
static inline uint64_t TIMER_TicksIn(uint64_t t0, uint64_t t1, uint64_t mask)
{
    const int sh = std::popcount(mask); // mask = 2^sh - 1
    return ((t1 + mask) >> sh) - ((t0 + mask) >> sh);
}

// Bit-exact equivalent of stepping every timer cycle (original loop):
// The timers are independent of each other and only set interrupt-pending
// bits, which the MCU evaluates before its next instruction. So the ticks of
// one timer within this call can be processed in bulk. A tick is uneventful
// if the counter matches neither compare register and does not overflow, and
// the timer's registers were not written since its last full tick (a write
// may enable an interrupt whose flag is already set, which the next tick
// must raise). Uneventful ticks only increment the counter; all other ticks
// run the original logic.
// --- Per-timer bulk advance -------------------------------------------------
// Bit-exact equivalent of stepping every timer cycle (original loop):
// The timers are independent of each other and only set interrupt-pending
// bits, which the MCU evaluates before its next instruction. A tick is
// uneventful if the counter matches neither compare register and does not
// overflow, and the timer's registers were not written since its last full
// tick (a write may enable an interrupt whose flag is already set, which the
// next tick must raise). Uneventful ticks only increment the counter; all
// other ticks run the original logic. Each timer is advanced lazily: only
// when its next eventful tick is due or when the MCU accesses the timers.

static void TIMER_AdvanceFrt(mcu_timer_t& timer, int i, uint64_t t1)
{
    const uint64_t t0 = timer.tpos[i];
    if (t1 <= t0) return;
    frt_t& frt = timer.frt[i];
    const uint64_t mask = timer.frt_step_table[frt.tcr & (FRT_TCR_CKS0 | FRT_TCR_CKS1)];
    uint64_t n = TIMER_TicksIn(t0, t1, mask);
    while (n > 0)
    {
        if (timer.frt_dirty[i] || frt.frc == frt.ocra || frt.frc == frt.ocrb || frt.frc == 0xffff)
        {
            TIMER_TickFrt(timer, i);
            timer.frt_dirty[i] = false;
            --n;
            continue;
        }
        const uint64_t da = (uint16_t)(frt.ocra - frt.frc);
        const uint64_t db = (uint16_t)(frt.ocrb - frt.frc);
        const uint64_t dv = (uint16_t)(0xffff - frt.frc);
        uint64_t k = da < db ? da : db;
        if (dv < k) k = dv;
        if (n < k) k = n;
        frt.frc = (uint16_t)(frt.frc + k);
        n -= k;
    }
    timer.tpos[i] = t1;
}

static void TIMER_AdvanceTmr(mcu_timer_t& timer, uint64_t t1)
{
    const uint64_t t0 = timer.tpos[3];
    if (t1 <= t0) return;
    tmr_t& tmr = timer.tmr;
    const uint64_t mask = timer.tmr_step_table[tmr.tcr & (TMR_TCR_CKS0 | TMR_TCR_CKS1 | TMR_TCR_CKS2)];
    if (mask != 0)
    {
        uint64_t n = TIMER_TicksIn(t0, t1, mask);
        while (n > 0)
        {
            if (timer.tmr_dirty || tmr.tcnt == tmr.tcora || tmr.tcnt == tmr.tcorb || tmr.tcnt == 0xff)
            {
                TIMER_TickTmr(timer);
                timer.tmr_dirty = false;
                --n;
                continue;
            }
            const uint64_t da = (uint8_t)(tmr.tcora - tmr.tcnt);
            const uint64_t db = (uint8_t)(tmr.tcorb - tmr.tcnt);
            const uint64_t dv = (uint8_t)(0xff - tmr.tcnt);
            uint64_t k = da < db ? da : db;
            if (dv < k) k = dv;
            if (n < k) k = n;
            tmr.tcnt = (uint8_t)(tmr.tcnt + k);
            n -= k;
        }
    }
    timer.tpos[3] = t1;
}

// Earliest timer cycle with an eventful tick for one timer (from its tpos)
static uint64_t TIMER_NextEventOf(const mcu_timer_t& timer, int i)
{
    const uint64_t t = timer.tpos[i];
    if (i < 3)
    {
        const frt_t&   frt  = timer.frt[i];
        const uint64_t mask = timer.frt_step_table[frt.tcr & (FRT_TCR_CKS0 | FRT_TCR_CKS1)];
        const uint64_t first = (t + mask) & ~mask; // first tick cycle >= t
        uint64_t d = 0;
        if (!timer.frt_dirty[i] && frt.frc != frt.ocra && frt.frc != frt.ocrb && frt.frc != 0xffff)
        {
            const uint64_t da = (uint16_t)(frt.ocra - frt.frc);
            const uint64_t db = (uint16_t)(frt.ocrb - frt.frc);
            const uint64_t dv = (uint16_t)(0xffff - frt.frc);
            d = da < db ? da : db;
            if (dv < d) d = dv;
        }
        return first + d * (mask + 1);
    }
    const tmr_t&   tmr  = timer.tmr;
    const uint64_t mask = timer.tmr_step_table[tmr.tcr & (TMR_TCR_CKS0 | TMR_TCR_CKS1 | TMR_TCR_CKS2)];
    if (mask == 0) return UINT64_MAX;
    const uint64_t first = (t + mask) & ~mask;
    uint64_t d = 0;
    if (!timer.tmr_dirty && tmr.tcnt != tmr.tcora && tmr.tcnt != tmr.tcorb && tmr.tcnt != 0xff)
    {
        const uint64_t da = (uint8_t)(tmr.tcora - tmr.tcnt);
        const uint64_t db = (uint8_t)(tmr.tcorb - tmr.tcnt);
        const uint64_t dv = (uint8_t)(0xff - tmr.tcnt);
        d = da < db ? da : db;
        if (dv < d) d = dv;
    }
    return first + d * (mask + 1);
}

static inline void TIMER_AdvanceOne(mcu_timer_t& timer, int i, uint64_t t1)
{
    if (i < 3) TIMER_AdvanceFrt(timer, i, t1);
    else TIMER_AdvanceTmr(timer, t1);
    timer.tnext[i] = TIMER_NextEventOf(timer, i);
}

static inline void TIMER_UpdateNextEvent(mcu_timer_t& timer)
{
    uint64_t ne = timer.tnext[0];
    for (int i = 1; i < 4; i++) if (timer.tnext[i] < ne) ne = timer.tnext[i];
    timer.next_event = ne;
}

// Bring ALL timers up to timer.target (state the original code would have
// here); used before any timer register access.
static void TIMER_Sync(mcu_timer_t& timer)
{
    for (int i = 0; i < 4; i++) TIMER_AdvanceOne(timer, i, timer.target);
    timer.cycles = timer.target;
    TIMER_UpdateNextEvent(timer);
}

void TIMER_Clock(mcu_timer_t& timer, uint64_t cycles)
{
    timer.target = (cycles + 1) / 2; // original: while (timer.cycles * 2 < cycles)
    // A tick at cycle c is processed once target > c; only advance the
    // timers whose event is due.
    if (timer.target > timer.next_event)
    {
        for (int i = 0; i < 4; i++)
            if (timer.target > timer.tnext[i]) TIMER_AdvanceOne(timer, i, timer.target);
        TIMER_UpdateNextEvent(timer);
    }
}

// These tables are indexed by the low CKSn bits of the TCR.
constexpr FRT_Step_Table FRT_STEP_TABLE_GENERIC = {3, 7, 31, 1};
constexpr FRT_Step_Table FRT_STEP_TABLE_MK1     = {3, 7, 31, 3};

// A value of 0 means do not step.
constexpr TMR_Step_Table TMR_STEP_TABLE_GENERIC = {0, 7, 63, 1023, 0, 1, 1, 1};
constexpr TMR_Step_Table TMR_STEP_TABLE_MK1     = {0, 7, 63, 1023, 0, 3, 3, 3};

void TIMER_NotifyRomsetChange(mcu_timer_t& timer)
{
    const bool is_mk1    = timer.mcu->is_mk1;
    timer.frt_step_table = is_mk1 ? FRT_STEP_TABLE_MK1 : FRT_STEP_TABLE_GENERIC;
    timer.tmr_step_table = is_mk1 ? TMR_STEP_TABLE_MK1 : TMR_STEP_TABLE_GENERIC;
    timer.next_event     = 0;
}
