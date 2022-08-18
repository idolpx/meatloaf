
#include <I2Cbus.hpp>


#define P00  	0
#define P01  	1
#define P02  	2
#define P03  	3
#define P04  	4
#define P05  	5
#define P06  	6
#define P07  	7
#define P10  	8
#define P11  	9
#define P12  	10
#define P13  	11
#define P14  	12
#define P15  	13
#define P16  	14
#define P17  	15

#define FLAG2  P07  // B
#define CNT1   P06  // 4
#define SP1    P05  // 5
#define CNT2   P04  // 6
#define SP2    P03  // 7
#define PC2    P02  // 8
#define ATN    P01  // 9
#define PA2    P00  // M

#define PB0    P10   // C
#define PB1    P11   // D
#define PB2    P12   // E
#define PB3    P13   // F
#define PB4    P14   // H - G
#define PB5    P15   // J - H
#define PB6    P16   // K - I
#define PB7    P17   // L - J




void parallel_setup ()
{
    i2c0.begin(GPIO_NUM_21, GPIO_NUM_22, 400000);  // sda, scl, 400 Khz

    
} 