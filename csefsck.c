//Alex Thomson // 4/16/2015 // File System Checker
//compiled with gcc -o csefsck -Wall -std=c99 csefsck.c

//Objectives:
   //Check Device ID
   //All Times are in Past
   //Free Block List
      //Used Blocks not present
      //All Other Blocks Present
   //Validate . and .. references 
   //linkcount
   //Validate indirect
   //Validate Size
      //Not indirect - size <= 4096
      //Indirect

#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "time.h"
#include "unistd.h"
#include "fcntl.h"
#define FILENAME_SIZE 512
#define DEVID 20
#define BLOCK_SIZE 4096
#define MAX_FILE_SIZE 1638400
#define NUM_INODE_FIELDS 10
#define NUM_DIR_FIELDS 9
#define NUM_SBLK_FIELDS 7
#define BASE_FILENAME "fusedata/fusedata."

//SuperBlock
struct sblk
{
  int ctime;
  int mounted;
  int devid;
  int fstart;
  int fend;
  int root;
  int maxBlocks;
};

struct ent
{
  char type;
  char name[FILENAME_SIZE];
  int blockNum;
  struct ent* next;
};

struct dir
{
  int size;
  int uid;
  int gid;
  int mode;
  int atime;
  int ctime;
  int mtime;
  int linkcount;
  struct ent* files;
};

struct file
{
  int size;
  int uid;
  int gid;
  int mode;
  int linkcount;
  int atime;
  int ctime;
  int mtime;
  int indirect;
  int location;
};

void valDir(int blk, int parent, time_t currTime, int* uBlks, int* size);
void valFile(int blk, time_t currTime, int* uBlks, int* size);
void addUBlk(int blk, int* ublks, int* size);
int openBlk(int blk);
void parSblk(int fd, struct sblk* sblk);
void parDir(int fd, struct dir* dir);
void parFile(int fd, struct file* file);

int main()
{
//	printf("Opening Superblock\n");
  struct sblk sblk;
  int sFile = openBlk(0);
  parSblk(sFile, &sblk);
  close(sFile);

  if(sblk.devid != DEVID)
    fprintf(stderr, "%s%d\n", "Error: Incorrect Device ID:", sblk.devid);
  
  time_t currTime = time(NULL);
  if(sblk.ctime > currTime)
    fprintf(stderr, "%s\n", "Error: File System creation time in future");

  
  int usedBlocks[sblk.maxBlocks];
  int freeBlocks[sblk.maxBlocks];
  int size = sblk.fend + 2;
  for(int i = 0; i <= sblk.fend + 1; i++)
    usedBlocks[i] = i;
  
  valDir(sblk.root, sblk.root, currTime, usedBlocks, &size);

  //for(int j = 0; j < size; j++)
  //	  fprintf(stderr, "Entry %d: %d\n", j, usedBlocks[j]);
  char* token;
  int numFree = 0;

  for(int i = sblk.fstart; i <= sblk.fend; i++)
    {
      int fd = openBlk(i);
      char buffer[BLOCK_SIZE];
      memset(buffer, 0, BLOCK_SIZE);
      read(fd, buffer, BLOCK_SIZE);
      close(fd);

      token = strtok(buffer, ",");
      freeBlocks[numFree] = atoi(token+1); 
      numFree++;     
      token = strtok(NULL, ",");
      while(token)
	{
	  freeBlocks[numFree++] = atoi(token);
	  token = strtok(NULL, ",");
	}
    }
  //for(int j = 0; j < numFree; j++)
  //  fprintf(stderr, "Entry %d: %d\n", j, freeBlocks[j]);
  /*
  int hasBlocksInUse = 0;
  int numIncorrect = 0;
  for(int i = 0; i < size; i++)
    {
      for(int j = 0; j < numFree; j++)
	{
	  if(freeBlocks[j] == usedBlocks[i])
	    {
	      hasBlocksInUse = 1;
	      numIncorrect++;
	      fprintf(stderr, "Error: Block %d is in use and on the free block list\n", usedBlocks[i]);
	    }
	}
    }
  */
  
  for(int i = 0; i < sblk.maxBlocks; i++)
    {
      int inFree = 0;
      int inUsed = 0;
      for(int j = 0; j < size; j++)
	{
	  if(usedBlocks[j] == i)
	    {
	      inUsed = 1;
	      break;
	    }
	}
      for(int k = 0; k < numFree; k++)
	{
	  if(freeBlocks[k] == i)
	    {
	      inFree = 1;
	      break;
	    }
	}
      if(inUsed && inFree)
	fprintf(stderr, "Error: Block %d is in use and on the free block list\n", i);
      else if(!inUsed && !inFree)
	fprintf(stderr, "Error: Block %d is missing from the free block list\n", i);
    }
}

void valDir(int blk, int parent, time_t currTime, int* uBlks, int* size)
{
  
  struct dir dir;
  int fd = openBlk(blk);
  parDir(fd, &dir);
  close(fd);
  if(dir.atime > currTime)
    fprintf(stderr, "Error: Directory in block %d contains an access time in the future\n",  blk);
  if(dir.ctime > currTime)
    fprintf(stderr, "Error: Directory in block %d contains a creation time in the future\n",  blk);
  if(dir.mtime > currTime)
    fprintf(stderr, "Error: Directory in block %d contains a modification time in the future\n",  blk);

  int lncnt = 0;
  struct ent* cEnt = dir.files;
  while(cEnt)
    {
      if(strcmp(cEnt->name, ".") == 0)
	{
		lncnt++;
	  if(cEnt->blockNum != blk)
	    fprintf(stderr, "Error: Directory in block %d contains incorrect \".\" reference to block %d\n",
		    blk, cEnt->blockNum);
	}
      else if(strcmp(cEnt->name, "..") == 0)
	{

	lncnt++;
	if(cEnt->blockNum != parent)
	  fprintf(stderr, "Error: Directory in block %d contains incorrect \"..\" reference to block %d\n",
		  blk, cEnt->blockNum);
	}
      else if(cEnt->type == 'd')
	  {
	    addUBlk(cEnt->blockNum, uBlks, size);
	    valDir(cEnt->blockNum, blk, currTime, uBlks, size); 
	    lncnt++;
	  }

      else if(cEnt->type == 'f')
	{
	  addUBlk(cEnt->blockNum, uBlks, size);
	  valFile(cEnt->blockNum, currTime, uBlks, size);
	}
      cEnt = cEnt->next;
    }
  if(lncnt != dir.linkcount)
    fprintf(stderr, "Error: Directory in block %d has incorrect linkcount of %d.\n       Correct linkcount: %d\n", blk,dir.linkcount, lncnt);
  
}

void valFile(int blk, time_t currTime, int* uBlks, int* size)
{
  struct file file;
  int fd = openBlk(blk);
  parFile(fd, &file);
  close(fd);
if(file.atime > currTime)
    fprintf(stderr, "Error: File inode in block %d contains an access time in the future\n",  blk);
  if(file.ctime > currTime)
    fprintf(stderr, "Error: File inode in block %d contains a creation time in the future\n",  blk);
  if(file.mtime > currTime)
    fprintf(stderr, "Error: File inode in block %d contains a modification time in the future\n",  blk);

  addUBlk(file.location, uBlks, size);

  fd = openBlk(file.location);
  char buffer[BLOCK_SIZE];
  memset(buffer, 0, BLOCK_SIZE);
  read(fd, buffer, BLOCK_SIZE);
  close(fd);
  char* token;
  token = strtok(buffer, ",");
  int val;
  int numBlks = 0;
  int indArr[400];//running low on time
  int isInd = file.indirect;
  if(!file.indirect) //search for block array
    {
      if((val = atoi(token)))
	{
	fprintf(stderr, "Error: File inode in block %d should have indirect set to 1\n", blk);
	isInd = 1;
	}
    }
  else
    {
      if(!(val = atoi(token)))
	{
	  fprintf(stderr, "Error: No array found. File inode in block %d should have indirect set to 0\n", blk);
	  isInd = 0;
	}
      
    }

  if(isInd)
    {
      indArr[0] = val; 
      numBlks++;     
      token = strtok(NULL, ",");
      while(token)
	{
	  indArr[numBlks++] = atoi(token);
	  token = strtok(NULL, ",");
	}

      //add indirect blocks
      for(int i = 0; i < numBlks; i++)
	addUBlk(indArr[i], uBlks, size);
//      fprintf(stderr, "File in %d has num %d\n", blk, numBlks);
      if(file.size > BLOCK_SIZE*numBlks)
	fprintf(stderr, "Error: File inode in block %d is indirect and has a filesize larger than\n       the %d block(s) allocated to it\n", blk, numBlks);

      if(file.size < BLOCK_SIZE*(numBlks - 1))
	fprintf(stderr, "Error: File inode in block %d is indirect and has a filesize smaller\n       than %d block(s). %d block(s) are allocated to it\n", blk, numBlks - 1,numBlks );
    }
  else
    {   
      if(file.size > BLOCK_SIZE)
	fprintf(stderr, "Error: File inode in block %d is not indirect but has filesize larger than 1 block length\n", blk);
    }
  
}

void addUBlk(int blk, int* ublks, int* size)
{
  for(int i = 0; i < *size; i++)
    {
      if(ublks[i] == blk)
	return;
    }
  ublks[(*size)++] = blk;
}

int openBlk(int blk)
{
	/* printf("Opening %d\n", blk); */
  char fileName[FILENAME_SIZE];
  sprintf(fileName, "%s%d", BASE_FILENAME, blk);
  return open(fileName, O_RDONLY);
}

void parSblk(int fd, struct sblk* sblk)
{
  char buffer[BLOCK_SIZE];
  memset(buffer, 0, BLOCK_SIZE);
   read(fd, buffer, BLOCK_SIZE);
   //fprintf(stderr, "BUFFER: %s\n", buffer);
  char* tokens[NUM_SBLK_FIELDS];

  tokens[0] = strtok(buffer, ",");
int i;
  for(i = 1; i < NUM_SBLK_FIELDS - 1; i++)
    tokens[i] = strtok(NULL, ",");
    
  tokens[NUM_SBLK_FIELDS - 1] = strtok(NULL, "}");

  for(i = 0; i < NUM_SBLK_FIELDS; i++)
    {
      strtok(tokens[i], ":");
      tokens[i] = strtok(NULL, ":");
      //fprintf(stderr, "%s\n", tokens[i]);
    }

  sblk->ctime = atoi(tokens[0]);
  sblk->mounted = atoi(tokens[1]);
  sblk->devid = atoi(tokens[2]);
  sblk->fstart = atoi(tokens[3]);
  sblk->fend = atoi(tokens[4]);
  sblk->root = atoi(tokens[5]);
  sblk->maxBlocks = atoi(tokens[6]);
}


void parFile(int fd, struct file* file)
{
  char buffer[BLOCK_SIZE];
  memset(buffer, 0, BLOCK_SIZE);

  int readVal = read(fd, buffer, BLOCK_SIZE);
  if(readVal < 0)
    fprintf(stderr, "%s", "READ ERROR\n");

  char* tokens[NUM_INODE_FIELDS];
  tokens[0] = strtok(buffer, ",");
  
  int i;
  for(i = 1; i < NUM_INODE_FIELDS - 2; i++)
    tokens[i] = strtok(NULL, ",");

  
  tokens[NUM_INODE_FIELDS - 2] = strtok(NULL, " ");
 
  tokens[NUM_INODE_FIELDS - 1] = strtok(NULL, "}");
 
  for(i = 0; i < NUM_INODE_FIELDS; i++)
    {
      strtok(tokens[i], ":");
      tokens[i] = strtok(NULL, ":");
      //fprintf(stderr, "%s\n", tokens[i]);
    }
 
  file->size = atoi(tokens[0]);
  file->uid = atoi(tokens[1]);
  file->gid = atoi(tokens[2]);
  file->mode = atoi(tokens[3]);
  file->linkcount = atoi(tokens[4]);
  file->atime = atoi(tokens[5]);
  file->ctime = atoi(tokens[6]);
  file->mtime = atoi(tokens[7]);
  file->indirect = atoi(tokens[8]);

  file->location = atoi(tokens[9]);

}

void parDir(int fd, struct dir* dir)
{
  //fprintf(stderr, "%s\n", "filllDir!");
  char buffer[BLOCK_SIZE];
  memset(buffer, 0, BLOCK_SIZE);
  
  int readVal = read(fd, buffer, BLOCK_SIZE);
//  printf("%s\n", buffer);
  if(readVal < 0)
    fprintf(stderr, "%s", "READ ERROR\n");

  char* tokens[NUM_DIR_FIELDS];
  tokens[0] = strtok(buffer, ",");

  int i;
  for(i = 1; i < NUM_DIR_FIELDS - 1; i++)
    tokens[i] = strtok(NULL, ",");
    
  tokens[NUM_DIR_FIELDS - 1] = strtok(NULL, "}");

  for(i = 0; i < NUM_DIR_FIELDS - 1; i++)
    {
      strtok(tokens[i], ":");
      tokens[i] = strtok(NULL, ":");
//      fprintf(stderr, "tokens[%d]: %s\n", i, tokens[i]);
    }
  
  dir->size = atoi(tokens[0]);
  dir->uid = atoi(tokens[1]);
  dir->gid = atoi(tokens[2]);
  dir->mode = atoi(tokens[3]);
  dir->atime = atoi(tokens[4]);
  dir->ctime = atoi(tokens[5]);
  dir->mtime = atoi(tokens[6]);
  dir->linkcount = atoi(tokens[7]);
  
  strtok(tokens[NUM_DIR_FIELDS - 1], ":");
  //strtok(NULL, "{");
  char* fileDict = strtok(NULL, "{");
//  fprintf(stderr, "lncnt: %d, %s\n", dir->linkcount, fileDict);

  //Fill up files list
  struct ent* current;
  struct ent* last;
  tokens[0] = strtok(fileDict, ":");
//  printf("tokens[0] %s\n", tokens[0]);
  if(tokens[0])
    {
      dir->files = malloc(sizeof(struct ent));
      last = dir->files;
      tokens[1] = strtok(NULL, ":");
      tokens[2] = strtok(NULL, ",");
      last->type = tokens[0][0];
      //strtok(NULL, ":");
      tokens[0] = strtok(NULL, ":");
      strcpy(last->name, tokens[1]);
      
      last->blockNum = atoi(tokens[2]);
      //    fprintf(stderr, "File Obj:%c,%s,%d\n", last->type, last->name, last->blockNum);      
    }

  while(tokens[0])
    {
      current = malloc(sizeof(struct ent));
      tokens[1] = strtok(NULL, ":");
      tokens[2] = strtok(NULL, ",");
//      fprintf(stderr, "%s, %s\n", tokens[1], tokens[2]);

      current->type = tokens[0][0];
      strcpy(current->name, tokens[1]);
      current->blockNum = atoi(tokens[2]);
//      fprintf(stderr, "File Obj: %c, %s, %d\n", current->type, current->name, current->blockNum);

      tokens[0] = strtok(NULL, ":");
      last->next = current;
      last = current;
    }
  last->next = NULL;
}
