//
//  test_lap.c
//  
//
//  Created by BlairWu on 2018/5/24.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <time.h>


#ifdef SV_TEST
#include "fpga_pci_sv.h"
#else
#include <fpga_pci.h>
#include <fpga_mgmt.h>
#include <utils/lcd.h>
#endif

#include <utils/sh_dpi_tasks.h>

/* Constants determined by the CL */
/* a set of register offsets; this CL has only one */
/* these register addresses should match the addresses in */
/* /aws-fpga/hdk/cl/examples/common/cl_common_defines.vh */
/* SV_TEST macro should be set if SW/HW co-simulation should be enabled */

// You could modify the register address once you finished the CL.
#define LAP_REG_ADDR UINT64_C(0x500)

#define M 64
#define N 16
#define K 64

/* use the stdout logger for printing debug information  */
#ifndef SV_TEST
const struct logger *logger = &logger_stdout;
/*
 * pci_vendor_id and pci_device_id values below are Amazon's and avaliable to use for a given FPGA slot.
 * Users may replace these with their own if allocated to them by PCI SIG
 */
static uint16_t pci_vendor_id = 0x1D0F; /* Amazon PCI Vendor ID */
static uint16_t pci_device_id = 0xF000; /* PCI Device ID preassigned by Amazon for F1 applications */

/*
 * check if the corresponding AFI for lap is loaded
 */
int check_afi_ready(int slot_id);
/*
 * An example to attach to an arbitrary slot, pf, and bar with register access.
 */

int peek_poke_example(uint32_t mat1[M][N], uint32_t mat2[N][K], int slot_id, int pf_id, int bar_id);

void usage(char* program_name) {
    printf("usage: %s [--slot <slot-id>][<poke-value>]\n", program_name);
}

void multiply(uint32_t mat1[M][N], uint32_t mat2[N][K], uint32_t res[M][K]);

#endif

// This function multiplies mat1[][] and mat2[][],
// and stores the result in res[][]
void multiply(uint32_t mat1[M][N], uint32_t mat2[N][K], uint32_t res[M][K])
{
    int i, j, k;
    for (i = 0; i < M; i++)
    {
        for (j = 0; j < K; j++)
        {
            res[i][j] = 0;
            for (k = 0; k < N; k++)
                res[i][j] += mat1[i][k]*mat2[k][j];
        }
    }
}

#ifdef SV_TEST
void test_main(uint32_t *exit_code) {
#else
int main(int argc, char **argv) {
#endif
    //The statements within SCOPE ifdef below are needed for HW/SW co-simulation with VCS
    #ifdef SCOPE
        svScope scope;
        scope = svGetScopeFromName("tb");
        svSetScope(scope);
    #endif
    
    int slot_id = 0;
    int rc;
    
#ifndef SV_TEST
    // Process command line args
    {
        int i;
        int value_set = 0;
        for (i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--slot")) {
                i++;
                if (i >= argc) {
                    printf("error: missing slot-id\n");
                    usage(argv[0]);
                    return 1;
                }
                sscanf(argv[i], "%d", &slot_id);
            } else if (!value_set) {
                sscanf(argv[i], "%x", &value);
                value_set = 1;
            } else {
                printf("error: Invalid arg: %s", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
    }
#endif
    
    /* initialize the fpga_pci library so we could have access to FPGA PCIe from this applications */
    rc = fpga_pci_init();
    fail_on(rc, out, "Unable to initialize the fpga_pci library");
    
#ifndef SV_TEST
    rc = check_afi_ready(slot_id);
#endif
    
    fail_on(rc, out, "AFI not ready");
    
    /* Accessing the CL registers via AppPF BAR0, which maps to sh_cl_ocl_ AXI-Lite bus between AWS FPGA Shell and the CL*/
    
    uint32_t A[M][N] = {0};
    uint32_t B[N][K] = {0};
    //uint32_t C[M][K] = {0};
    
    srand(time(NULL));
    int i, j;
    for (i = 0; i < M; i++) {
        for (j = 0; j < N; j++) {
            // You could modify 10 here.
            // This is just a range for the randomly generated matrix element.
            A[i][j] = (uint32_t)rand() % 10;
        }
    }
    
    srand(time(NULL));
    for (i = 0; i < N; i++) {
        for (j = 0; j < K; j++) {
            // You could modify 10 here.
            // This is just a range for the randomly generated matrix element.
            B[i][j] = (uint32_t)rand() % 10;
        }
    }
    
    /*srand(time(NULL));
    for (i = 0; i < M; i++) {
        for (j = 0; j < K; j++) {
            // You could modify 10 here.
            // This is just a range for the randomly generated matrix element.
            C[i][j] = (uint32_t)rand() % 10;
        }
    }*/

    printf("===== Starting with peek_poke_example =====\n");
    
    rc = peek_poke_example(A, B, slot_id, FPGA_APP_PF, APP_PF_BAR0);
    fail_on(rc, out, "peek-poke example failed");
    
#ifndef SV_TEST
    return rc;
    
out:
    return 1;
#else
    
out:
    *exit_code = 0;
#endif
}
    
/* As HW simulation test is not run on a AFI, the below function is not valid */
#ifndef SV_TEST
    
int check_afi_ready(int slot_id) {
    struct fpga_mgmt_image_info info = {0};
    int rc;
    
    /* get local image description, contains status, vendor id, and device id. */
    rc = fpga_mgmt_describe_local_image(slot_id, &info,0);
    fail_on(rc, out, "Unable to get AFI information from slot %d. Are you running as root?",slot_id);
    
    /* check to see if the slot is ready */
    if (info.status != FPGA_STATUS_LOADED) {
        rc = 1;
        fail_on(rc, out, "AFI in Slot %d is not in READY state !", slot_id);
    }
    
    printf("AFI PCI  Vendor ID: 0x%x, Device ID 0x%x\n",
           info.spec.map[FPGA_APP_PF].vendor_id,
           info.spec.map[FPGA_APP_PF].device_id);
    
    /* confirm that the AFI that we expect is in fact loaded */
    if (info.spec.map[FPGA_APP_PF].vendor_id != pci_vendor_id ||
        info.spec.map[FPGA_APP_PF].device_id != pci_device_id) {
        printf("AFI does not show expected PCI vendor id and device ID. If the AFI "
               "was just loaded, it might need a rescan. Rescanning now.\n");
        
        rc = fpga_pci_rescan_slot_app_pfs(slot_id);
        fail_on(rc, out, "Unable to update PF for slot %d",slot_id);
        /* get local image description, contains status, vendor id, and device id. */
        rc = fpga_mgmt_describe_local_image(slot_id, &info,0);
        fail_on(rc, out, "Unable to get AFI information from slot %d",slot_id);
        
        printf("AFI PCI  Vendor ID: 0x%x, Device ID 0x%x\n",
               info.spec.map[FPGA_APP_PF].vendor_id,
               info.spec.map[FPGA_APP_PF].device_id);
        
        /* confirm that the AFI that we expect is in fact loaded after rescan */
        if (info.spec.map[FPGA_APP_PF].vendor_id != pci_vendor_id ||
            info.spec.map[FPGA_APP_PF].device_id != pci_device_id) {
            rc = 1;
            fail_on(rc, out, "The PCI vendor id and device of the loaded AFI are not "
                    "the expected values.");
        }
    }
    
    return rc;
out:
    return 1;
}
    
#endif

/*
 * An example to attach to an arbitrary slot, pf, and bar with register access.
 */
int peek_poke_example(uint32_t mat1[M][N], uint32_t mat2[N][K], int slot_id, int pf_id, int bar_id) {
    uint32_t res[M][K] = {0};
    clock_t tic = clock();
    // expected result
    multiply(mat1, mat2, res);
    clock_t toc = clock();
    printf("Elapsed: %f seconds\n", (double)(toc - tic) / CLOCKS_PER_SEC);

    int rc;
    /* pci_bar_handle_t is a handler for an address space exposed by one PCI BAR on one of the PCI PFs of the FPGA */
    
    pci_bar_handle_t pci_bar_handle = PCI_BAR_HANDLE_INIT;
    
    
    /* attach to the fpga, with a pci_bar_handle out param
     * To attach to multiple slots or BARs, call this function multiple times,
     * saving the pci_bar_handle to specify which address space to interact with in
     * other API calls.
     * This function accepts the slot_id, physical function, and bar number
     */
#ifndef SV_TEST
    rc = fpga_pci_attach(slot_id, pf_id, bar_id, 0, &pci_bar_handle);
#endif
    fail_on(rc, out, "Unable to attach to the AFI on slot id %d", slot_id);
    
    tic = clock();
    uint32_t dimensions = 0;
    uint32_t byte1 = 1 << 31;
    uint32_t byte2 = M << 16;
    uint32_t byte3 = N << 8;
    dimensions |= (byte1 | byte2 | byte3 | K);
    rc = fpga_pci_poke(pci_bar_handle, LAP_REG_ADDR, dimensions);
    fail_on(rc, out, "Unable to write to the fpga !");
    
    int i, j;
    uint32_t value;
    // write values to mapped address space
    for (i = 0; i < M; i++) {
        for (j = 0; j < N; j++) {
            value = mat1[i][j];
            //printf("Writing 0x%08x to LAP register (0x%016lx)\n", value, LAP_REG_ADDR);
            rc = fpga_pci_poke(pci_bar_handle, LAP_REG_ADDR, value);
            fail_on(rc, out, "Unable to write to the fpga !");
        }
    }
    
    for (i = 0; i < N; i++) {
        for (j = 0; j < K; j++) {
            value = mat2[i][j];
            //printf("Writing 0x%08x to LAP register (0x%016lx)\n", value, LAP_REG_ADDR);
            rc = fpga_pci_poke(pci_bar_handle, LAP_REG_ADDR, value);
            fail_on(rc, out, "Unable to write to the fpga !");
        }
    }
    
    /*for (int i = 0; i < M; i++) {
        for (int j = 0; j < K; j++) {
            uint32_t value = mat3[i][j];
            //printf("Writing 0x%08x to LAP register (0x%016lx)\n", value, LAP_REG_ADDR);
            rc = fpga_pci_poke(pci_bar_handle, LAP_REG_ADDR, value);
            fail_on(rc, out, "Unable to write to the fpga !");
        }
    }*/
    
    printf("=====  Entering peek_poke_example =====\n");
    uint32_t signal = 0;
    while (signal == 0) {
        rc = fpga_pci_peek(pci_bar_handle, LAP_REG_ADDR, &signal);
        fail_on(rc, out, "Unable to read from the fpga !");
    }
    printf("Computation done!\n");
    toc = clock();
    printf("with FPGA -- Elapsed: %f seconds\n", (double)(toc - tic) / CLOCKS_PER_SEC);

    int pass = 1;
    // read values back and compare with res
    for (i = 0; i < M; i++) {
        for (j = 0; j < K; j++) {
            //uint32_t value;
            if (i == 0 && j == 0) {
                value = signal;
            }else{
                rc = fpga_pci_peek(pci_bar_handle, LAP_REG_ADDR, &value);
                fail_on(rc, out, "Unable to read from the fpga !");
            }
            if (value != res[i][j]) {
                printf("TEST FAILED");
                printf("Resulting value did not match expected value 0x%x. Something didn't work.\n", res[i][j]);
                pass = 0;
                break;
            }
        }
        if (!pass) {
            break;
        }
    }
    if (pass) {
        printf("TEST PASSED");
    }

out:
    /* clean up */
    if (pci_bar_handle >= 0) {
        rc = fpga_pci_detach(pci_bar_handle);
        if (rc) {
            printf("Failure while detaching from the fpga.\n");
        }
    }
    
    /* if there is an error code, exit with status 1 */
    return (rc != 0 ? 1 : 0);
}


