// Testing the file system
// John Tadrous
// August 9, 2020
#include "tm4c123gh6pm.h"
#include "tm4c123gh6pm_def.h"
#include "OS_File_System.h"
uint8_t File0, File1, File_Size;
uint8_t Data[512];
uint8_t Process_FB;


int main(void){
  uint8_t i=0;
	// Initializing the Disk
  OS_FS_Init();
  OS_File_Format();
  
  // Creating File0
  File0=OS_File_New();    // Create File0
  
  // Data for File
  for (i=0; i<12; i++){
    Data[i]=i;
  }
  Process_FB=OS_File_Append(File0, Data);
  // Creating File1
  File1=OS_File_New();    // Create File1
  // Data for File
  for (i=0; i<100; i++){
    Data[i]=100-i;
  }
  Process_FB=OS_File_Append(File1, Data);
  for (i=0; i<200; i++){
    Data[i]=i+3;
  }
  Process_FB=OS_File_Append(File0, Data);
  
  File_Size=OS_File_Size(File0);
  Process_FB=OS_File_Read(File0, 1, Data);
  Process_FB=OS_File_Read(File0, 2, Data);
  Process_FB=OS_File_Read(File1, 0, Data);
  
  Process_FB=OS_File_Append(File0, Data);
  for (i=0; i<200; i++){
    Data[i]=i/2+2;
  }
  Process_FB=OS_File_Append(File0, Data);
  Process_FB=OS_File_Read(File0, 2, Data);
  Process_FB=OS_File_Read(File1, 0, Data);
  File_Size=OS_File_Size(File0);
  
  OS_File_Flush();
  
}
