// Test several flash access characteristics and draw insights
// By John Tadrous
// August 6, 2020

#include "TM4C123GH6PM.h"
#include "TM4C123GH6PM_def.h"
#include "FlashProgram.h"

#define FLASH_Array_Size		1024

int	Access_FB;   // access feedback
uint32_t FLASH_Scalar;
uint32_t RAM_Scalar;
uint32_t FLASH_Read_Data;
uint32_t FLASH_Access_Address=0x20000;
uint32_t FLASH_Array [FLASH_Array_Size];

int main(void){
	// 1) RAM vs FLASH Access
  Flash_Erase(FLASH_Access_Address);
	RAM_Scalar=0x20;
	FLASH_Scalar=0x30;
	Access_FB=Flash_Write(FLASH_Access_Address, FLASH_Scalar);  // Write one word.
	
	
	// 2) Write a word to the access address+1
	FLASH_Scalar=0x35;
	Access_FB=Flash_Write(FLASH_Access_Address+1, FLASH_Scalar);
  Access_FB=Flash_Write(FLASH_Access_Address, FLASH_Scalar);
  
	// 3) Writing an array
	for (int i=0; i<FLASH_Array_Size; i++){
		FLASH_Array[i]=2*i+1;
	}
  Flash_Erase(FLASH_Access_Address+0x400);
	Access_FB=Flash_WriteArray(FLASH_Array, FLASH_Access_Address+0x400, FLASH_Array_Size);
	
	// 4) Flash Erase
	Access_FB=Flash_Erase(FLASH_Access_Address+0x404);
	
}
