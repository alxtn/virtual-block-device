// Alex Thomson  //  N16520134    3/30/2015    OS Filesystems Project

//Compile with: gcc -Wall alexThomson.c `pkg-config fuse --cflags --libs` -o alexThomson

#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

//Constants
#define MAX_BLOCK_COUNT 10000
#define MAX_FILE_SIZE 1638400
#define BLOCK_SIZE 4096
#define MAX_FILES 1000
#define NUM_DIR_FIELDS 9
#define NUM_INODE_FIELDS 10
#define FILENAME_SIZE 512
#define BASE_FILENAME "fusedata/fusedata." 
#define PATH_MAX 1024
#define SMALL_BUF 128

//Maps path to block number by walking down directory tree
//must specify 'f' or 'd' in type
int pathToBlock(const char* path, char type);


//parses a path, fills dest with parent path and
//child with the last part of the path
void getParent(const char* path, char* dest, char* child);

//used to store data about the free blocklist
//placed in private data field
//of fuse context
struct myContext
{
  int numListBlocks;
  int entPerBlock;
  int numDigits;
};

//one entry in the filename to inode dict
//piece of linked list
struct dictEntry
{
  char type;
  char name[FILENAME_SIZE];
  int blockNum;
  struct dictEntry* next;
};

struct direc
{
  int size;
  int uid;
  int gid;
  int mode;
  int atime;
  int ctime;
  int mtime;
  int linkcount;
  //linked list of filename to inode entries
  struct dictEntry* files;
};

struct inodeData
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

//Fills inode file from structure
void fillInodeFile(int fd, struct inodeData* inode)
{
  char buffer[BLOCK_SIZE];
  char value[64];
  memset(buffer, 0, BLOCK_SIZE);
  
  strcpy(buffer, "{");
  strcat(buffer, "size:");
  sprintf(value, "%d", inode->size);
  strcat(buffer, value);
  strcat(buffer, ", uid:");
  sprintf(value, "%d", inode->uid);
  strcat(buffer, value);
  strcat(buffer, ", gid:");
  sprintf(value, "%d", inode->gid);
  strcat(buffer, value);
  strcat(buffer, ", mode:");
  sprintf(value, "%d", inode->mode);
  strcat(buffer, value);
  strcat(buffer, ", linkcount:");
  sprintf(value, "%d", inode->linkcount);
  strcat(buffer, value);
  strcat(buffer, ", atime:");
  sprintf(value, "%d", inode->atime);
  strcat(buffer, value);
  strcat(buffer, ", ctime:");
  sprintf(value, "%d", inode->ctime);
  strcat(buffer, value);
  strcat(buffer, ", mtime:");
  sprintf(value, "%d", inode->mtime);
  strcat(buffer, value);
  strcat(buffer, ", indirect:");
  sprintf(value, "%d", inode->indirect);
  strcat(buffer, value);
  strcat(buffer, ", location:");
  sprintf(value, "%d", inode->location);
  strcat(buffer, value);
  strcat(buffer, "}");
  write(fd, buffer, BLOCK_SIZE);
}

//parses file to fill inode structure
void fillInode(int fd, struct inodeData* inode)
{
  char buffer[BLOCK_SIZE];
  memset(buffer, 0, BLOCK_SIZE);

  int readVal = read(fd, buffer, BLOCK_SIZE);
  if(readVal < 0)
    fprintf(stderr, "%s", "READ ERROR\n");

  char* tokens[NUM_INODE_FIELDS];
  tokens[0] = strtok(buffer, ",");

  int i;
  for(i = 1; i < NUM_INODE_FIELDS - 1; i++)
    tokens[i] = strtok(NULL, ",");
    
  tokens[NUM_INODE_FIELDS - 1] = strtok(NULL, "}");

  for(i = 0; i < NUM_INODE_FIELDS; i++)
    {
      strtok(tokens[i], ":");
      tokens[i] = strtok(NULL, ":");
      //fprintf(stderr, "%s\n", tokens[i]);
    }
  inode->size = atoi(tokens[0]);
  inode->uid = atoi(tokens[1]);
  inode->gid = atoi(tokens[2]);
  inode->mode = atoi(tokens[3]);
  inode->linkcount = atoi(tokens[4]);
  inode->atime = atoi(tokens[5]);
  inode->ctime = atoi(tokens[6]);
  inode->mtime = atoi(tokens[7]);
  inode->indirect = atoi(tokens[8]);
  inode->location = atoi(tokens[9]);
}

//Fills file from direc structure
void fillDirFile(int fd, struct direc* dir)
{
  char buffer[BLOCK_SIZE];
  char value[SMALL_BUF];
  memset(buffer, 0, BLOCK_SIZE);
  
  strcpy(buffer, "{");
  strcat(buffer, "size:");
  sprintf(value, "%d", dir->size);
  strcat(buffer, value);
  strcat(buffer, ", uid:");
  sprintf(value, "%d", dir->uid);
  strcat(buffer, value);
  strcat(buffer, ", gid:");
  sprintf(value, "%d", dir->gid);
  strcat(buffer, value);
  strcat(buffer, ", mode:");
  sprintf(value, "%d", dir->mode);
  strcat(buffer, value);
  strcat(buffer, ", atime:");
  sprintf(value, "%d", dir->atime);
  strcat(buffer, value);
  strcat(buffer, ", ctime:");
  sprintf(value, "%d", dir->ctime);
  strcat(buffer, value);
  strcat(buffer, ", mtime:");
  sprintf(value, "%d", dir->mtime);
  strcat(buffer, value);
  strcat(buffer, ", linkcount:");
  sprintf(value, "%d", dir->linkcount);
  strcat(buffer, value);
  strcat(buffer, ", filename_to_inode_dict:{");//{f:foo:1234, d:.:102, d:..:10, f:bar:2245}");
  
  struct dictEntry* current = dir->files;
  while(current)
    {
      sprintf(value, "%c", current->type);
      strcat(buffer, value);
      strcat(buffer, ":");
      strcat(buffer, current->name);
      strcat(buffer, ":");
      sprintf(value, "%d", current->blockNum);
      strcat(buffer, value);
      strcat(buffer, ",");
      current = current->next;
    }
  buffer[strlen(buffer) - 1] = '\0';
  strcat(buffer, "}}");
  write(fd, buffer, BLOCK_SIZE);
}

//parses file to fill direc structure
void fillDirec(int fd, struct direc* dir)
{
  //fprintf(stderr, "%s\n", "filllDir!");
  char buffer[BLOCK_SIZE];
  memset(buffer, 0, BLOCK_SIZE);
  
  int readVal = read(fd, buffer, BLOCK_SIZE);
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
      //fprintf(stderr, "%s\n", tokens[i]);
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
  char* fileDict = strtok(NULL, "{");
  //fprintf(stderr, "%s\n", fileDict);

  //Fill up files list
  struct dictEntry* current;
  struct dictEntry* last;
  tokens[0] = strtok(fileDict, ":");

  if(tokens[0])
    {
      dir->files = malloc(sizeof(struct dictEntry));
      last = dir->files;
      tokens[1] = strtok(NULL, ":");
      tokens[2] = strtok(NULL, ",");
      last->type = tokens[0][0];
      tokens[0] = strtok(NULL, ":");
      
      strcpy(last->name, tokens[1]);
      last->blockNum = atoi(tokens[2]);
      fprintf(stderr, "File Obj:%c,%s,%d\n", last->type, last->name, last->blockNum);      
    }

  while(tokens[0])
    {
      current = malloc(sizeof(struct dictEntry));
      tokens[1] = strtok(NULL, ":");
      tokens[2] = strtok(NULL, ",");
      //fprintf(stderr, "%s, %s\n", tokens[1], tokens[2]);

      current->type = tokens[0][0];
      strcpy(current->name, tokens[1]);
      current->blockNum = atoi(tokens[2]);
      fprintf(stderr, "File Obj: %c, %s, %d\n", current->type, current->name, current->blockNum);

      tokens[0] = strtok(NULL, ":");
      last->next = current;
      last = current;
    }
  last->next = NULL;
}


struct dictEntry* searchFileDict(struct dictEntry* fileList, char* name)
{
  struct dictEntry* current = fileList;
  while(current)
    {
      if(strcmp(current->name, name) == 0)
	return current;
      current = current->next;
    }
  return NULL;
}

//gets the next free block
//updates free block list
int getBlock()
{
  char buffer[BLOCK_SIZE];
  char tokBuffer[BLOCK_SIZE];
  char writeBuffer[BLOCK_SIZE];
  memset(writeBuffer, 0, BLOCK_SIZE);
  char* token;
  int blockNum = -1;
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;
  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  int blockFile;
  int i;
  for(i = 1; i <= myData->numListBlocks; i++)
    {
      sprintf(fileName, "%s%d", BASE_FILENAME, i);
      blockFile = open(fileName, O_RDONLY);
      read(blockFile, buffer, BLOCK_SIZE);
      fprintf(stderr, "%s: %s\n", fileName, buffer);
      close(blockFile);
      if(strncmp(buffer, "{}", 2) != 0)
	  break;
      else if(i == myData->numListBlocks)
	return blockNum;
    }
  strcpy(tokBuffer, buffer);
  token = strtok(tokBuffer, ",");
  if(token[strlen(token) - 1] == '}')
    {
      token = strtok(token, "}");
      blockNum = atoi(&token[1]);
      fprintf(stderr, "%s%d", "Only One Left!:", blockNum);
      
      strcpy(writeBuffer, "{}");
      blockFile = open(fileName, O_WRONLY);
      write(blockFile, writeBuffer, BLOCK_SIZE);
      close(blockFile);
    }
  else
    {
      blockNum = atoi(&token[1]);
      fprintf(stderr, "%s%d\n", "Block Returned:", blockNum);
      strcpy(writeBuffer, "{");
      strcat(writeBuffer, &token[strlen(token) + 1]);
      blockFile = open(fileName, O_WRONLY);
      write(blockFile, writeBuffer, BLOCK_SIZE);
      close(blockFile);
    }


  return blockNum;
}

//Adds a block to the free block list
//in the appropriate place
void freeBlock(int blockNum)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;
  char buffer[BLOCK_SIZE];
  char tokBuffer[BLOCK_SIZE];
  char writeBuffer[BLOCK_SIZE];
  memset(writeBuffer, 0, BLOCK_SIZE);
  char* token;
  char value[64];
  int blocks[myData->entPerBlock];

  int fileNum = blockNum / myData->entPerBlock + 1;
  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  sprintf(fileName, "%s%d", BASE_FILENAME, fileNum);
  int blockFile = open(fileName, O_RDONLY);
  read(blockFile, buffer, BLOCK_SIZE);
  close(blockFile);
  fprintf(stderr, "%s\n", buffer);
  strcpy(tokBuffer, buffer);
  token = strtok(tokBuffer, ",");
  if(token[strlen(token) - 1] == '}')
    {
      strcpy(writeBuffer, "{");
      sprintf(value, "%d", blockNum);
      strcat(writeBuffer, value);
      strcat(writeBuffer, "}");

    }
  else
    {
      int firstBlock = atoi(&token[1]);
      if(blockNum < firstBlock)
	{
	  strcpy(writeBuffer, "{");
	  sprintf(value, "%d,", blockNum);
	  strcat(writeBuffer, value);
	  strcat(writeBuffer, &buffer[1]);
	}
      else
	{
	  blocks[0] = firstBlock;
	  int i = 0;
	  while(i < myData->entPerBlock && blockNum > blocks[i])
	    {
	      i++;
	      token = strtok(NULL, ",");
	      blocks[i] = atoi(token);
	      if(blocks[i] == blockNum)
		return;
	    }
	  int offset = token - tokBuffer;
	  strncpy(writeBuffer, buffer, offset);
	  sprintf(value, "%d,", blockNum);
	  strcat(writeBuffer, value);
	  strcat(writeBuffer, &buffer[offset]);
	}

    }
      blockFile = open(fileName, O_WRONLY);
      write(blockFile, writeBuffer, BLOCK_SIZE);
      close(blockFile);
}

//Creates initial free block list dictionaries
char* dictGen(int start, int numEntries, int numDigits)
{
  fprintf(stderr, "%s\n", "dictGen");
  char* dict = malloc(sizeof(char) * BLOCK_SIZE);
  memset(dict, 0, BLOCK_SIZE);
  dict[0] = '{';
  dict[1] = '\0';
  int i;
  char entry[numDigits + 2];
  memset(entry, 0, numDigits + 2);
  for(i = 0; i < numEntries; i++)
    {
      sprintf(entry, "%d,", start + i);
      strcat(dict, entry);
    }
  int end = strlen(dict);
  dict[end-1] = '}';
  //dict[end] = '\0';
  //fprintf(stderr, "%s\n", dict);
  return dict;
}

//Creates a superblock
void genSuperBlock(char* dict, int numListBlocks, int numDigits, int  timeVal, int mounted)
{
  fprintf(stderr, "%s\n", "gensuper");
  char value[numDigits];      
  sprintf(value, "%d", numListBlocks);
  dict[0] = '{';
  strcat(dict, "creationTime:");
  if(!timeVal)
    {
      time_t* currentTime = malloc(sizeof(time_t));
      *currentTime = time(NULL);
      sprintf(value, "%d", (int)*currentTime);
      free(currentTime);
    }
  else
    sprintf(value, "%d", timeVal);

  strcat(dict, value);
  
  strcat(dict, ", mounted:");
  sprintf(value, "%d", mounted);
  strcat(dict, value);
  strcat(dict, ", devId:20, freeStart:1, freeEnd:");
  sprintf(value, "%d", numListBlocks);
  strcat(dict, value);
  strcat(dict, ", root:");
  sprintf(value, "%d", numListBlocks + 1);
  strcat(dict, value);
  strcat(dict, ", maxBlocks:");
  sprintf(value, "%d", MAX_BLOCK_COUNT);
  strcat(dict, value);
  strcat(dict, "}");
  fprintf(stderr, "%s\n", dict);
  
}


//Gets attributes of specified file
int getattribs(const char* path, struct stat* statbuf)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;
  fprintf(stderr, "%s\n", "getattribs");
  memset(statbuf, 0, sizeof(struct stat));
  //statbuf->st_mode = S_IFDIR | 0755;
  //statbuf->st_nlink = 2;
  int isFile = 1;
  int block = pathToBlock(path, 'f');
  
  if(block < 0)
    {
      block = pathToBlock(path, 'd');
      isFile = 0;
    }

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char value[64];
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", block);
  strcat(fileName, value);
  int blockFile = open(fileName, O_RDONLY);
  fprintf(stderr, "THIS IS PIVOTAL:%s isFile:%d\n", fileName, isFile);
  if(block < 0)
    {
      fprintf(stderr, "%s\n", "NotFound");
    return -ENOENT;
    }
  if(isFile)
    {
      struct inodeData* inode = malloc(sizeof(struct inodeData));
      fillInode(blockFile, inode);
      statbuf->st_nlink = inode->linkcount;
      statbuf->st_mode = S_IFREG | 0777;//inode->mode;
      statbuf->st_size = inode->size;
      statbuf->st_ino = block;
      statbuf->st_atime = inode->atime;
      statbuf->st_mtime = inode->mtime;
      statbuf->st_ctime = inode->ctime;
      free(inode);
    }
  else
    {
      struct direc* dir = malloc(sizeof(struct direc));
      fillDirec(blockFile, dir);
      statbuf->st_dev = 20;
      statbuf->st_nlink = dir->linkcount;
      statbuf->st_mode = S_IFDIR | dir->mode;
      statbuf->st_size = dir->size;
      //statbuf->st_ino = block; //apparently ignored by fuse
      statbuf->st_uid = dir->uid;
      statbuf->st_gid = dir->gid;
      statbuf->st_blksize = BLOCK_SIZE;
      statbuf->st_blocks = 20;
      statbuf->st_atime = dir->atime;
      statbuf->st_mtime = dir->mtime;
      statbuf->st_ctime = dir->ctime;
      free(dir);
    }
  close(blockFile);
  return 0;
}

//File inode number is stored as the file handle to be
//used later in other fuse functions
int openfile(const char* path, struct fuse_file_info* info)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  
  char child[128];
  getParent(path, fileName, child);
  int blockNum = pathToBlock(fileName, 'f');
  
  //store inode number in the filehandle variable
  info->fh = blockNum;

  if(blockNum < 0)
    return -ENOENT;
  else
    return 0;
}


int readfile(const char* path, char* buf, size_t size, off_t offset, 
	     struct fuse_file_info* info)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char value[myData->numDigits];
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", (int)info->fh);
  strcat(fileName, value);
  int inoFile = open(fileName, O_RDWR);

  struct inodeData inode;
  fillInode(inoFile, &inode);
  inode.atime = (int)time(NULL);
  lseek(inoFile, 0, SEEK_SET);
  fillInodeFile(inoFile, &inode);
  close(inoFile);

  //Simple, Direct Read
  if(!inode.indirect)
    {
     strcpy(fileName, BASE_FILENAME);
     sprintf(value, "%d", inode.location);
     strcat(fileName, value);
     int blockFile = open(fileName, O_RDONLY);
     int retVal = pread(blockFile, buf, size, offset);
     close(blockFile);
     return retVal;
    }

  //Indirect Read
  else
    {
      //parse indirect block list
      int startBlock = offset / BLOCK_SIZE;
      int newOffset = offset % BLOCK_SIZE;
      int retVal = 0;
      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", inode.location);
      strcat(fileName,value);
      char readBuf[BLOCK_SIZE];
      memset(readBuf, 0, BLOCK_SIZE);
      int blockFile = open(fileName, O_RDONLY);
      read(blockFile, readBuf, BLOCK_SIZE);
      close(blockFile);
      int blockNums[MAX_FILE_SIZE / BLOCK_SIZE];

      char* token;

      //remove { and }
      token = strtok(readBuf, "}");
      token = &token[1];
      int blockNum;
      int fileBlocks = 1;
      token = strtok(token, ",");
      blockNums[0] = atoi(token);
      token = strtok(NULL, ",");
      while(token)
	{
	  blockNums[fileBlocks] = atoi(token);
	  fileBlocks++;
	  token = strtok(NULL, ",");	  
	}

      //open first block needed
      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", blockNums[startBlock]);
      strcat(fileName,value);
      blockFile = open(fileName, O_RDONLY);

      //if only need this block
      if(newOffset + size <= BLOCK_SIZE)
	{
	  retVal = pread(blockFile, buf, size, newOffset);
	  close(blockFile);
	  return retVal;
	}
      //if multiple blocks are needed
      else
	{
	  retVal = pread(blockFile, buf, BLOCK_SIZE - newOffset, newOffset);
	  int gotten = BLOCK_SIZE - newOffset;
	  int remaining = size - gotten;
	  close(blockFile);

	  int count = 1;
	  while(remaining > 0)
	    {
	      blockNum = blockNums[startBlock + count];
	      strcpy(fileName, BASE_FILENAME);
	      sprintf(value, "%d", blockNum);
	      strcat(fileName,value);
	      blockFile = open(fileName, O_RDONLY);
	      if(remaining >= BLOCK_SIZE)
		retVal += read(blockFile, buf + gotten, BLOCK_SIZE);
	      else
		retVal += read(blockFile, buf + gotten, remaining);
	      gotten += BLOCK_SIZE;
	      remaining -= BLOCK_SIZE;
	      count++;
	      close(blockFile);
	    }
	  return retVal;
	}
    }

}

int openddir(const char* path, struct fuse_file_info* info)
{
  //fprintf(stderr, "%s\n", path);
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;
  //fprintf(stderr, "%s\n", "opendir!");
  if(strcmp("/", path) == 0)
    {
      //fprintf(stderr, "%s\n", "rootcheck!");
      info->fh = myData->numListBlocks + 1;
      return 0;
    }
  int blockNum = pathToBlock(path, 'd');
  if(blockNum < 0)
    return blockNum;

  //again, use block number as file handle
  info->fh = blockNum;
  return 0;
}

int readdirec(const char* path, void* buf, fuse_fill_dir_t filler, off_t off, 
	      struct fuse_file_info* info)
{
  //fprintf(stderr, "%s\n","readdirSTART");
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;
  time_t currentTime;
  currentTime = time(NULL);
  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char pathName[PATH_MAX];
  char value[myData->numDigits];
  //fprintf(stderr, "%d\n", (int)info->fh);
 
  //open dir file, fill dir structure
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", (int)info->fh);
  strcat(fileName, value);
  fprintf(stderr, "filename: %s\n", fileName);
  int dirFile = open(fileName, O_RDWR);
  struct direc* directory = malloc(sizeof(struct direc));
  fillDirec(dirFile, directory);
  directory->atime = (int)currentTime;
  lseek(dirFile, 0, SEEK_SET);
  fillDirFile(dirFile, directory);
  close(dirFile);

  //for each entry, use the fuse filler function
  //use getattr to fill stat structure
  struct dictEntry* currEnt = directory->files;
  while(currEnt)
    {
      strcpy(pathName, path);
      strcat(pathName, "/");
      strcat(pathName, currEnt->name);
      struct stat statbuf;
      getattribs(pathName, &statbuf);
      filler(buf, currEnt->name, &statbuf, 0);
      currEnt = currEnt->next;
    }
  free(directory);
  return 0;
}

int mkdirec(const char* path, mode_t mode)
{
  //open parent dir
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;
  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char pathName[PATH_MAX];
  char value[myData->numDigits];
  char child[SMALL_BUF];
  time_t currentTime;
  currentTime = time(NULL);
  getParent(path, pathName, child);
  
  int blockNum = pathToBlock(pathName, 'd');
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", blockNum);
  strcat(fileName, value);
  int dirFile = open(fileName, O_RDWR);
  struct direc* parent = malloc(sizeof(struct direc));
  
  fillDirec(dirFile, parent);
  lseek(dirFile, 0, SEEK_SET);
  struct dictEntry* currEnt = parent->files;
  struct dictEntry* lastEnt;
  struct dictEntry* newEnt = malloc(sizeof(struct dictEntry));
  while(currEnt)
    {
      lastEnt = currEnt;
      currEnt = currEnt->next;
    }

  //fill out new filename to inode dict entry 
  //for parent's dictionary
  strcpy(newEnt->name, child);
  newEnt->blockNum = getBlock();
  newEnt->type = 'd';
  lastEnt->next = newEnt;
  newEnt->next = NULL;
  //fprintf(stderr, "New ent: %s %d\n", newEnt->name, newEnt->blockNum);
  parent->linkcount++;
  parent->mtime = (int) currentTime;
  fillDirFile(dirFile, parent);
  close(dirFile);

  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", newEnt->blockNum);
  strcat(fileName, value);
  dirFile = open(fileName, O_WRONLY);

  //fill out new directory info
  struct dictEntry de;
  de.type = 'd';
  strcpy(de.name, ".");
  de.blockNum = newEnt->blockNum;


  struct dictEntry de2;
  de2.type = 'd';
  strcpy(de2.name, "..");
  de2.blockNum = blockNum;
  de2.next = NULL;
  de.next = &de2;

  struct direc dir;
  dir.size = BLOCK_SIZE;
  dir.uid = context->uid;
  dir.gid = context->gid;
  dir.mode = S_IFDIR | mode;
  dir.atime = (int) currentTime;
  dir.ctime = (int) currentTime;
  dir.mtime = (int) currentTime;
  dir.linkcount = 2;
  dir.files = &de;

  //write to file
  fillDirFile(dirFile, &dir);

  close(dirFile);
  free(parent);
  free(newEnt);
  return 0;
}

int createFile(const char* path, mode_t mode, struct fuse_file_info* info)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char value[64];
  char child[128];
  int blockNum = getBlock();
  int oldBlockNum = blockNum;
  fprintf(stderr, "oldBlocknum: %d\n", oldBlockNum);
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", blockNum);
  strcat(fileName,value);
  
  time_t currentTime;
  currentTime = time(NULL);
  //sprintf(value, "%d", (int)currentTime);

  struct inodeData newNode;
  newNode.size = 0;
  newNode.uid = context->uid;
  newNode.gid = context->gid;
  newNode.mode = S_IFREG | mode;
  newNode.linkcount = 1;
  newNode.atime = (int) currentTime;
  newNode.mtime = (int) currentTime;
  newNode.ctime = (int) currentTime;
  newNode.indirect = 0;
  newNode.location = getBlock();

  info->fh = oldBlockNum;

  int inoFile = open(fileName, O_WRONLY);
  fillInodeFile(inoFile, &newNode);
  close(inoFile);
  char* newName = malloc(sizeof(char) * 4096);
  getParent(path, newName, child);
  fprintf(stderr, "Got parent(%d) dir!: %s\n",(int)newName, newName);
  blockNum = pathToBlock(newName, 'd');
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", blockNum);
  strcat(fileName,value);
  fprintf(stderr, "Got parent fileee!: %s\n", fileName);
  int dirFile = open(fileName, O_RDWR);
  struct direc* dir = malloc(sizeof(struct direc));
  fillDirec(dirFile, dir);
  struct dictEntry fileEnt;
  strcpy(fileEnt.name, child);
  fileEnt.blockNum = oldBlockNum;
  fileEnt.type = 'f';
  fileEnt.next = dir->files;
  dir->files = &fileEnt;
  lseek(dirFile, 0, SEEK_SET);
  fillDirFile(dirFile, dir);
  close(dirFile);
  free(newName);
  return 0;
}

//removes a file, simply frees all blocks
//belonging to the file and removes from parent's
//dictionary
int unlinkFile(const char* path)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char value[SMALL_BUF];
   
  char pathName[PATH_MAX];
  char child[SMALL_BUF];
  getParent(path, pathName, child);
  int fileToFree;
  int blockNum = pathToBlock(pathName, 'd');
  if(blockNum < 0)
    return -ENOENT;
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", blockNum);
  strcat(fileName,value);
  int blockFile = open(fileName, O_RDWR);


  struct direc dir;
  fillDirec(blockFile, &dir);

  struct dictEntry* currEnt = dir.files;
  struct dictEntry* lastEnt;

  if(strcmp(currEnt->name, child) == 0)  
    {
      dir.files = currEnt->next;
      fileToFree = currEnt->blockNum;
      currEnt = NULL;
    }
  
  while(currEnt)
    {
      if(strcmp(currEnt->name, child) == 0)
	{
	  lastEnt->next = currEnt->next;
	  fileToFree = currEnt->blockNum;
	}
      lastEnt = currEnt;
      currEnt = currEnt->next;
    }
  lseek(blockFile, 0, SEEK_SET);
  fillDirFile(blockFile, &dir);
  close(blockFile);

  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", fileToFree);
  strcat(fileName,value);
  int inoFile = open(fileName, O_RDWR);

  struct inodeData inode;
  fillInode(inoFile, &inode);

  //last hardlink, free
  if(inode.linkcount == 1)
    {
      close(inoFile);
      //extra work for indirect
      if(inode.indirect)
	{
	  strcpy(fileName, BASE_FILENAME);
	  sprintf(value, "%d", inode.location);
	  strcat(fileName,value);
	  char readBuf[BLOCK_SIZE];
	  memset(readBuf, 0, BLOCK_SIZE);
	  blockFile = open(fileName, O_RDONLY);
	  read(blockFile, readBuf, BLOCK_SIZE);
	  close(blockFile);
	  int blockNums[MAX_FILE_SIZE / BLOCK_SIZE];

	  char* token;

	  //remove { and }
	  token = strtok(readBuf, "}");
	  token = &token[1];
      
	  int fileBlocks = 1;
	  token = strtok(token, ",");
	  blockNums[0] = atoi(token);
	  token = strtok(NULL, ",");
	  while(token)
	    {
	      blockNums[fileBlocks] = atoi(token);
	      fileBlocks++;
	      token = strtok(NULL, ",");	  
	    }

	  int j;
	  for(j = 0; j < fileBlocks; j++)
	    {
	      freeBlock(blockNums[j]);
	    }
	}

      //both indirect and direct will free these two
      freeBlock(inode.location);
      freeBlock(fileToFree);
      return 0;
    }
  else
    {
      inode.linkcount--;
      lseek(inoFile, 0, SEEK_SET);
      fillInodeFile(inoFile, &inode);
      close(inoFile);
      return 0;
    }
}

//use block number as file handle
int openFile(const char* path, struct fuse_file_info* info)
{
  info->fh = pathToBlock(path, 'f');
  if(info->fh < 0)
    return -ENOENT;
  return 0;
}

int writeFile(const char* path, const char* buffer, size_t size, off_t offset,
	  struct fuse_file_info* info)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;
  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char value[SMALL_BUF];
   
  
  int blockNum = info->fh;
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", blockNum);
  strcat(fileName,value);
  fprintf(stderr, "Opening Inode file: %s\n", fileName);
  int inoFile = open(fileName, O_RDWR);
  int blockFile;
  struct inodeData inode;
  fillInode(inoFile, &inode);
  
  //simple, direct write
  if(!inode.indirect)
    {
      fprintf(stderr, "Writing directly to: fusedata.%d unless indirect\n", inode.location);
      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", inode.location);
      strcat(fileName,value);
      blockFile = open(fileName, O_WRONLY);

      //check if we need another block
      if(inode.size + size <= BLOCK_SIZE)
	{
	  int ret = pwrite(blockFile, buffer, size, offset);
	  inode.size+=size;
	  inode.mtime = (int)time(NULL);
	  lseek(inoFile, 0, SEEK_SET);
	  fillInodeFile(inoFile, &inode);
	  close(blockFile);
	  close(inoFile);
	  return ret; 

	}
      //if so, make indirect pointer block
      else
	{

	  close(blockFile);
	  inode.indirect = 1;
	  int firstBlock = inode.location;
	  inode.location = getBlock();

	  char indBuf[BLOCK_SIZE];
	  memset(indBuf, 0, BLOCK_SIZE);
	  strcpy(indBuf, "{");
	  sprintf(value, "%d", firstBlock);
	  strcat(indBuf, value);
	  strcat(indBuf, "}");

	  strcpy(fileName, BASE_FILENAME);
	  sprintf(value, "%d", inode.location);
	  strcat(fileName,value);
	  blockFile = open(fileName, O_WRONLY);
	  write(blockFile, indBuf, BLOCK_SIZE);
	  close(blockFile);

	  fillInodeFile(inoFile, &inode);
	  
	  
	  fprintf(stderr, "Converted to Indirect: indir:%d, firstblock:%d\n", inode.location, firstBlock);
	}

    }

  //inode.indirect is checked instead of using an else
  //statement because inode.indirect can change in the
  //first if statement if we try to make the file larger 
  //than BLOCK_SIZE
  if(inode.indirect)
    {
      int newSize = inode.size + size;
      if(newSize > MAX_FILE_SIZE)
	return -EFBIG;
      int blocksNeeded;
      if(newSize % BLOCK_SIZE)
	blocksNeeded = newSize / BLOCK_SIZE + 1;
      else
	blocksNeeded = newSize / BLOCK_SIZE;

      int startBlock = offset / BLOCK_SIZE;
      int newOffset = offset % BLOCK_SIZE;

      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", inode.location);
      strcat(fileName,value);
      char readBuf[BLOCK_SIZE];
      memset(readBuf, 0, BLOCK_SIZE);
      blockFile = open(fileName, O_RDWR);
      read(blockFile, readBuf, BLOCK_SIZE);

      int blockNums[MAX_FILE_SIZE / BLOCK_SIZE];

      char* token;

      //remove { and }
      token = strtok(readBuf, "}");
      token = &token[1];

      int fileBlocks = 1;
      int j = 1;
      token = strtok(token, ",");
      blockNums[0] = atoi(token);
      token = strtok(NULL, ",");
      while(token)
	{
	  blockNums[j] = atoi(token);
	  fileBlocks++;
	  token = strtok(NULL, ",");
	  j++;
	}

      //if new blocks are needed, allocate and update
      //block indirect pointer list
      int diff = blocksNeeded - fileBlocks;
      if(diff > 0)
	{
	  int k;
	  for(k = 0; k < diff; k++)
	    {
	      blockNums[j] = getBlock();
	      j++;
	    }

	  memset(readBuf, 0, BLOCK_SIZE);
	  strcpy(readBuf, "{");

	  for(k = 0; k < j - 1; k++)
	    {
	      sprintf(value, "%d,", blockNums[k]);
	      strcat(readBuf, value);
	    }
	  sprintf(value, "%d}", blockNums[j - 1]);
	  strcat(readBuf, value);
	  lseek(blockFile, 0, SEEK_SET);
	  write(blockFile, readBuf, BLOCK_SIZE);
	}
      close(blockFile);
      
      blockNum = blockNums[startBlock];
      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", blockNum);
      strcat(fileName,value);
      blockFile = open(fileName, O_WRONLY);

      //if fits into one block
      if((size + newOffset) <= BLOCK_SIZE)
	{
	  pwrite(blockFile, buffer, size, newOffset);
	  close(blockFile);
	}
      else
	{
	  pwrite(blockFile, buffer, BLOCK_SIZE - newOffset, newOffset);
	  int written = BLOCK_SIZE - newOffset;
	  int remaining = size - written;
	  close(blockFile);
	  int count = 1;
	  while(remaining > 0)
	    {
	      blockNum = blockNums[startBlock + count];
	      strcpy(fileName, BASE_FILENAME);
	      sprintf(value, "%d", blockNum);
	      strcat(fileName,value);
	      blockFile = open(fileName, O_WRONLY);
	      if(remaining >= BLOCK_SIZE)
		write(blockFile, buffer + written, BLOCK_SIZE);
	      else
		write(blockFile, buffer + written, remaining);
	      written += BLOCK_SIZE;
	      remaining -= BLOCK_SIZE;
	      count++;
	      close(blockFile);
	    }
	}


      inode.size+=size;
      inode.mtime = (int)time(NULL);
      lseek(inoFile, 0, SEEK_SET);
      fillInodeFile(inoFile, &inode);
      close(inoFile);
      return size;
    }
  //If the function gets here, something went wrong
  return -1;
}

//creates new link to file and updates linkcount
int linkFile(const char* oldPath, const char* newPath)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char* pathName = malloc(sizeof(char)*PATH_MAX);
  char value[myData->numDigits];
  char child[SMALL_BUF];
  
  int linkBlockNum = pathToBlock(oldPath, 'f');
  getParent(newPath, pathName, child);
  int blockNum = pathToBlock(pathName, 'd');
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", blockNum);
  strcat(fileName, value);
  fprintf(stderr, "Link:File Inode:%d  NewDir: fusedata.%d\n", linkBlockNum, blockNum);
  int dirFile = open(fileName, O_RDWR);

  struct direc dir;
  fillDirec(dirFile, &dir);

  struct dictEntry de;
  strcpy(de.name, child);
  de.type = 'f';
  de.blockNum = linkBlockNum;
  de.next = dir.files;
  dir.files = &de;

  lseek(dirFile, 0, SEEK_SET);
  fillDirFile(dirFile, &dir);
  close(dirFile);

  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", linkBlockNum);
  strcat(fileName, value);

  int inoFile = open(fileName, O_RDWR);
 
  struct inodeData inode;
  fillInode(inoFile, &inode);
  inode.linkcount++;
  lseek(inoFile, 0, SEEK_SET);
  fillInodeFile(inoFile, &inode);
  close(inoFile);
  return 0;
}

//renames, and moves if necessary
int renameFile(const char* old, const char* new)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char* parentOld = malloc(sizeof(char)*PATH_MAX);
  char* parentNew = malloc(sizeof(char)*PATH_MAX);
  char value[myData->numDigits];
  char oldName[SMALL_BUF];
  char newName[SMALL_BUF];
  int blockNum;
  getParent(old, parentOld, oldName);
  getParent(new, parentNew, newName);


  blockNum = pathToBlock(parentOld, 'd');
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", blockNum);
  strcat(fileName, value);

  int dirFile = open(fileName, O_RDWR);
  struct direc dir;
  fillDirec(dirFile, &dir);

  struct dictEntry* currEnt;
  currEnt = dir.files;

  //same parent directory
  if(strcmp(parentOld, parentNew) == 0)
    {
      while(currEnt)
	{
	  if(strcmp(currEnt->name, oldName) == 0)
	    {
	      strcpy(currEnt->name, newName);
	      break;
	    }
	  currEnt = currEnt->next;
	}
      lseek(dirFile, 0, SEEK_SET);
      fillDirFile(dirFile, &dir);
      close(dirFile);
      return 0;
    }
  //different parents
  else
    {
      int notFirst = 1;
      struct dictEntry* lastEnt;

      if(strcmp(currEnt->name, oldName) == 0)
	{
	  strcpy(currEnt->name, newName);
	  dir.files = currEnt->next;
	  notFirst = 0;
	}
      while(currEnt && notFirst)
	{
	  if(strcmp(currEnt->name, oldName) == 0)
	    {
	      strcpy(currEnt->name, newName);
	      lastEnt->next = currEnt->next;
	      break;
	    }
	  lastEnt = currEnt;
	  currEnt = currEnt->next;
	}
      lseek(dirFile, 0, SEEK_SET);
      fillDirFile(dirFile, &dir);
      close(dirFile);

      blockNum = pathToBlock(parentNew, 'd');
      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", blockNum);
      strcat(fileName, value);

      dirFile = open(fileName, O_RDWR);
      fillDirec(dirFile, &dir);

      currEnt->next = dir.files;
      dir.files = currEnt;
      lseek(dirFile, 0, SEEK_SET);
      fillDirFile(dirFile, &dir);
      close(dirFile);
      return 0;
    }

}

//initializes files system
//this function is a mess.... but it works
void* initfs(struct fuse_conn_info* conn)
{
  fprintf(stderr, "%s\n", "init");
  struct fuse_context* context= fuse_get_context();
 
  //Calculate Number of digits in max count and block counts
  int numDigits = 1;
  double blockNum = MAX_BLOCK_COUNT;
  while(blockNum > 1)
    {
      blockNum /= 10;
      numDigits++;
    }
  int entPerBlock = BLOCK_SIZE / (numDigits  * 20) * 10;
  int numListBlocks = MAX_BLOCK_COUNT / entPerBlock;


  //create file name strings
  fprintf(stderr, "%s", "\n\nINITIALIZING\n\n");
  char baseName[strlen(BASE_FILENAME)];
  char fileName[strlen(BASE_FILENAME) + numDigits];
  strcpy(baseName, BASE_FILENAME);
  strcpy(fileName, baseName);
  strcat(fileName, "0");


  //open/create the superblock
  int superblock;
  superblock = open(fileName, O_RDWR|O_CREAT|O_EXCL, S_IRWXU |  S_IRWXG | S_IRWXO );
  fprintf(stderr, "%s%d\n", "superblock: ", superblock);
  
  char initBuf[BLOCK_SIZE];
  //If the superblock is empty, create the block files
  if(superblock > 0 )//read(superblock, initBuf, BLOCK_SIZE) == 0)
    {
      //Create Buffer to initialize files with
      memset(initBuf, 0, BLOCK_SIZE);
      write(superblock, initBuf, BLOCK_SIZE);
      
      //Create Files
      int blockFile;
      int i;
      fprintf(stderr, "%s", "Creating Blocks\n");
      
      //Create each new block file
      for(i = 1; i < MAX_BLOCK_COUNT; i++)
	{
	  sprintf(fileName, "%s%d", baseName, i);
	  blockFile = open(fileName, O_WRONLY|O_CREAT,S_IRWXU | S_IRWXG | S_IRWXO);
	  write(blockFile, initBuf, BLOCK_SIZE);
	  close(blockFile);
	}
      


      //Create Free Block List
      for(i = 0; i < numListBlocks; i++)
	{
	  sprintf(fileName, "%s%d", baseName, i + 1);
	  blockFile = open(fileName, O_WRONLY);
	  if(blockFile >= 0)
	    {
	      char* dict;
	      if(i == 0)
		dict = dictGen(numListBlocks + 2, entPerBlock - numListBlocks - 2, numDigits);
	      else
		dict = dictGen(i * entPerBlock, entPerBlock, numDigits);	
	      write(blockFile, dict, strlen(dict));
	      free(dict);
	    }
	  else
	    fprintf(stderr, "%s %s\n", "Could not open Block file: ",fileName);
	  

	}
      //create root directory
      struct dictEntry de;
      de.type = 'd';
      strcpy(de.name, ".");
      de.blockNum = 26;

      struct dictEntry de2;
      de2.type = 'd';
      strcpy(de2.name, "..");
      de2.blockNum = 26;
      de2.next = NULL;
      de.next = &de2;
      time_t currentTime;
      currentTime = time(NULL);
      struct direc dir;
      dir.size = BLOCK_SIZE;
      dir.uid = context->uid;
      dir.gid = context->gid;
      dir.mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
      dir.atime = (int)currentTime;
      dir.ctime = (int)currentTime;
      dir.mtime = (int)currentTime;
      dir.linkcount = 2;
      dir.files = &de;

      char path2[PATH_MAX];
      char num2[SMALL_BUF];
      strcpy(path2, BASE_FILENAME);
      sprintf(num2,"%d", numListBlocks + 1);
      strcat(path2, num2);
      int rootFile = open(path2, O_WRONLY);
      if(!rootFile)
	fprintf(stderr, "%s", "failure to open file\n");
      fillDirFile(rootFile, &dir);
      //fprintf(stderr, "%d\n", rootFile);
      close(rootFile);


      //Fill superblock with necessary info
      char dict[BLOCK_SIZE];
      memset(dict, 0, BLOCK_SIZE);
      genSuperBlock(dict, numListBlocks, numDigits, 0, 1);
      lseek(superblock, 0, SEEK_SET);
      write(superblock, dict, strlen(dict));
    } 
  //if fusedata.0 already existed
  else
    {
      //fprintf(stderr, "initbuf: %s\n", initBuf);
      //close(superblock);
      superblock = open(fileName, O_RDWR);
      if(!superblock)
	fprintf(stderr, "%s", "Error opening superblock\n");

      //increment mount count
      char superDict[BLOCK_SIZE];
      char* token;
      char* tokenbuf = malloc(sizeof(char) * BLOCK_SIZE);
      int mounted;
      int time;
      
      if(read(superblock, superDict, BLOCK_SIZE)  <  0)
	{
	  fprintf(stderr, "%s", "ERROR Reading\n");
	  perror("READING");  
	  //return NULL;
	}
      fprintf(stderr, "%s", "Read successfully?\n" );
      fprintf(stderr, "dict: %s\n", superDict);
      token = strtok(superDict, ",");
      if(!token)
	{
	  fprintf(stderr, "%s", "Error tokenizing\n");
	  //perror("DA");
	  return NULL;
	}
      while(token)
	{
	  if(strncmp(token, "{creationTime", strlen("{creationTime")) == 0)
	    {
	      strcpy(tokenbuf, token);
	    }
	  else if(strncmp(token, " mounted", strlen(" mounted")) == 0)
	    {
	      token = strtok(token, ":");
	      token = strtok(NULL, ":");
	      mounted = atoi(token);
	      fprintf(stderr, "The Value of Mounted is: %d\n", mounted);
	      mounted++;
	      
	    }
	 
	  token = strtok(NULL, ",");
	}
      token = strtok(tokenbuf, ":");
      token = strtok(NULL, ":");
      fprintf(stderr, "%s", "BEFORE ATOI\n");
      time = atoi(token);
      memset(superDict, 0, BLOCK_SIZE);
      genSuperBlock(superDict, numListBlocks, numDigits, time, mounted);
      //close(superblock);
      lseek(superblock, 0, SEEK_SET);
      //superblock = open(fileName, O_WRONLY | O_TRUNC);
      write(superblock, superDict, BLOCK_SIZE);
      free(tokenbuf);
    }
  close(superblock);

  struct myContext* myData = malloc(sizeof(struct myContext));
  myData->numListBlocks = numListBlocks;
  myData->entPerBlock = entPerBlock;
  myData->numDigits = numDigits;
  return myData;
}

//frees up my private data from the fuse context
void cleanupFS(void* data)
{
  free(data);
}

//These two functions just return 0
//I did not store any private data in the fuse context 
//when opening files, so there is nothing I need to free up here
int releaseFile(const char* path, struct fuse_file_info* info)
{
  return 0;
}
int releaseDir(const char* path, struct fuse_file_info* info)
{
  return 0;
}

//gets info about filesystem
int statFileSys(const char* path, struct statvfs* statbuf)
{
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char value[myData->numDigits];
  char buffer[BLOCK_SIZE];
  char* token;
  int i;
  int freeBlocks = 0;

  //calculate free blocks left
  for(i = 0; i < myData->numListBlocks; i++)
    {
      memset(buffer, 0, BLOCK_SIZE);
      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", i + 1);
      strcat(fileName, value);
      int blockFile = open(fileName, O_RDONLY);
      read(blockFile, buffer, BLOCK_SIZE);
      close(blockFile);
      if(strcmp(buffer, "{}") == 0)
	continue;
      token = strtok(buffer, ",");
      while(token)
	{
	  freeBlocks++;
	  token = strtok(NULL, ",");
	}
    }

  

  statbuf->f_bsize = BLOCK_SIZE;
  statbuf->f_blocks = MAX_BLOCK_COUNT;
  statbuf->f_bfree = freeBlocks;
  statbuf->f_bavail = freeBlocks;
  statbuf->f_files = (MAX_BLOCK_COUNT - freeBlocks) / 2;
  statbuf->f_ffree = freeBlocks;
  statbuf->f_namemax = SMALL_BUF;
  return 0;
}


struct fuse_operations my_ops = { 
  .create = createFile,
  .destroy= cleanupFS,
  .getattr = getattribs,
  .init = initfs,
  .link = linkFile,
  .mkdir = mkdirec,
  .open = openFile,
  .opendir = openddir,
  .read = readfile,
  .readdir = readdirec,
  .release = releaseFile,
  .releasedir = releaseDir,
  .rename = renameFile,
  .statfs = statFileSys,
  .unlink = unlinkFile,
  .write = writeFile
};

int main(int argc, char *argv[])
{
  fprintf(stderr, "%s\n", "Main");
  return fuse_main(argc, argv, &my_ops, NULL);
}


//described at beginning of file
int pathToBlock(const char* path, char type)
{  
  fprintf(stderr, "Start ptb(%c): %s\n",type, path);
  struct fuse_context* context= fuse_get_context();
  struct myContext* myData = context->private_data;

  //short circuit for root
  if(strcmp(path, "/") == 0)
    {
      if(type == 'd')
	return myData->numListBlocks + 1;
      else
	return -ENOENT;
    }
  
  char fileName[strlen(BASE_FILENAME) + myData->numDigits];
  char pathName[PATH_MAX];
  strcpy(pathName, path);
  strcat(pathName, "/");
  fprintf(stderr, "ptb(%c): %s\n",type, pathName);
  char value[SMALL_BUF];
  int currentFile;
  char* token; //Just to have a value for first loop iteration
  int blockNum;

  //open root directory
  strcpy(fileName, BASE_FILENAME);
  sprintf(value, "%d", myData->numListBlocks + 1);
  strcat(fileName, value);
  currentFile = open(fileName, O_RDONLY);
  if(!currentFile)
    {
      fprintf(stderr, "%s", "Failed to open root inode file\n");
      return -1;
    }
 
  int initial = 1;
  
  struct direc* currentDir = malloc(sizeof(struct direc));
  struct dictEntry* currEnt;
  /* int i = 0;
  token = strtok(pathName, "/");
  for(i = 0; i < 3; i++)
    {
      fprintf(stderr, "testToken: %s\n", token);
      token = strtok(NULL, "/");
      }*/
  token = strtok(pathName, "/");
  while(1)
    {
      if(!initial)
	{
	  fprintf(stderr, "testToken: %s\n", "initial");
	  token = strtok(token + strlen(token) + 1, "/");
	  
	}
      else
	initial = 0;
      
      fprintf(stderr, "Loopstart:Token: %s\n", token);
      if(!token)
	{
	  fprintf(stderr, "%s\n", "Breakin");
	  break;
	}
      fillDirec(currentFile, currentDir);
      close(currentFile);
      
      currEnt = currentDir->files;
      /*if(!initial)
	{
	  fprintf(stderr, "testToken: %s\n", "initial");
	  token = strtok(token + strlen(token) + 1, "/");
	  
	}
      else
	initial = 0;
      
      fprintf(stderr, "Loopstart:Token: %s\n", token);
      if(!token)
	{
	  fprintf(stderr, "%s\n", "Breakin");
	  break;
	  }*/
      while(currEnt)
	{
	  fprintf(stderr, "Entry: %s token:%s\n", currEnt->name, token);
	  if(strcmp(token, currEnt->name) == 0)
	    {
	      fprintf(stderr, "%s: type:'%c'\n", "Match", currEnt->type);
	      blockNum = currEnt->blockNum;
	      break;	
	    }
	 
	  currEnt = currEnt->next;
	}
      if(!currEnt)
	{
	  fprintf(stderr, "%s\n", "NOT PRESENT!");
	  free(currentDir);
	  return -1;
	}
      strcpy(fileName, BASE_FILENAME);
      sprintf(value, "%d", blockNum);
      strcat(fileName, value);
      fprintf(stderr, "opening: %s\n", fileName);
      currentFile = open(fileName, O_RDONLY);
      
    }
  free(currentDir);
  
  if(currEnt->type == type)
    {
      fprintf(stderr, "%s: %d\n", "Return Success", blockNum);
      return blockNum;
    }
  else
    {
      fprintf(stderr, "%s\n", "Failed Type");
      return -1;
    }
}

//described at beginning of file
void getParent(const char* path, char* dest, char* child)
{
  char pathName[PATH_MAX];
  strcpy(pathName, path);
 
  
  char* token = strtok(pathName, "/");
  char* lastToken = token;
  while(token)
    {
      lastToken = token;
      token = strtok(NULL, "/");
    }
  int offset = lastToken - pathName;
  memset(dest, 0, PATH_MAX);
  strncpy(dest, path, offset);
  if(child)
    {
      strcpy(child, &path[offset]);
      fprintf(stderr, "Parent:%d: %s, Child:%s\n", (int)dest, dest, child);
    }
}
