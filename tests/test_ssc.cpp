#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "core/Common.h"
#include "apple2/SerialComms.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"
#include <cstring>
#include <cstdint>

// Mock CPU functions
void CpuIrqAssert(eIRQSRC source) { (void)source; }
void CpuIrqDeassert(eIRQSRC source) { (void)source; }
void CpuNmiAssert(eIRQSRC source) { (void)source; }
void CpuNmiDeassert(eIRQSRC source) { (void)source; }

// Mock Memory functions
void RegisterIoHandler(uint32_t slot, iofunction r, iofunction w, iofunction cr, iofunction cw, void* p, uint8_t* rom) {
    (void)slot; (void)r; (void)w; (void)cr; (void)cw; (void)p; (void)rom;
}
auto IO_Null(uint16_t nPC, uint16_t nAddr, uint8_t nWriteFlag, uint8_t nWriteValue, uint32_t nCyclesLeft) -> uint8_t {
    (void)nPC; (void)nAddr; (void)nWriteFlag; (void)nWriteValue; (void)nCyclesLeft;
    return 0;
}

// Mock Frontend functions
static uint8_t g_lastSentByte = 0;
static bool g_sendCalled = false;
void SSCFrontend_SendByte(uint8_t byte) {
    g_lastSentByte = byte;
    g_sendCalled = true;
}
auto SSCFrontend_IsActive() -> bool { return true; }
void SSCFrontend_UpdateState(uint32_t b, uint32_t s, int p, int t) {
    (void)b; (void)s; (void)p; (void)t;
}

static bool g_irqAsserted = false;
static void MockAssertIrq(int slot, bool assert) {
    (void)slot;
    g_irqAsserted = assert;
}

extern Peripheral_t g_ssc_peripheral;

TEST_CASE("SSC: Status Register Bit 4 (TDRE) Set On Reset") {
    HostInterface_t host;
    memset(&host, 0, sizeof(host));
    host.AssertIrq = MockAssertIrq;
    
    void* instance = g_ssc_peripheral.init(2, &host);
    REQUIRE(instance != nullptr);

    g_sendCalled = false;

    // Hardware Reset
    g_ssc_peripheral.reset(instance);

    // Check status
    uint8_t status = SSC_IORead(instance, 0, 0xC0A9, 0, 0, 0); 
    CHECK((status & (1 << 4)) != 0); // TDRE should be 1 (Empty)
    
    g_ssc_peripheral.shutdown(instance);
}

TEST_CASE("SSC: Transmit Sets TDRE Interrupt") {
    HostInterface_t host;
    memset(&host, 0, sizeof(host));
    host.AssertIrq = MockAssertIrq;
    
    void* instance = g_ssc_peripheral.init(2, &host);
    REQUIRE(instance != nullptr);

    g_sendCalled = false;
    g_irqAsserted = false;

    // 1. Enable Transmit Interrupts
    SSC_IOWrite(instance, 0, 0xC0AA, 1, 0x04, 0); 

    // 2. Write to Transmit Data Register
    SSC_IOWrite(instance, 0, 0xC0A8, 1, 0x41, 0); 
    CHECK(g_irqAsserted == true);

    // 3. Check status
    uint8_t status = SSC_IORead(instance, 0, 0xC0A9, 0, 0, 0); 
    CHECK((status & (1 << 4)) != 0);
    CHECK(g_irqAsserted == false); 
    
    g_ssc_peripheral.shutdown(instance);
}
