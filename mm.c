/*
 * mm.c - The fastest, least memory-efficient malloc package.
 */
// heaplow and heaphigh start and end and mem_sbrk in mm.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
 
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define BLK_HDR_SIZE ALIGN(sizeof(blockHdr))

#define MIN_BLK_SIZE (BLK_HDR_SIZE + SIZE_T_SIZE)

#define FTPR(bp) (size_t *)((char *)bp + (bp->size & ~1L) - SIZE_T_SIZE)

typedef struct header blockHdr;

struct header {
  size_t size;
  blockHdr *next_p;
  blockHdr *prior_p; 
};

void *find_fit(size_t size);
void print_heap();
void *coalesce(blockHdr *bp);
void insert_blk(blockHdr *);
void remove_blk(blockHdr *);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  //called before everything else
  //this block is never allocated, just a sentinel
  size_t *fp;
  blockHdr *p = mem_sbrk(BLK_HDR_SIZE + SIZE_T_SIZE);
  p->size = (BLK_HDR_SIZE + SIZE_T_SIZE)& ~1L;
  p->next_p = p;
  p->prior_p = p;
  fp = (size_t *)((char *)p + p->size - SIZE_T_SIZE);
  *fp = p->size;
  return 0;
}

void print_heap() {
  blockHdr *bp = mem_heap_lo();
  size_t *fp;
  while(bp < (blockHdr *)mem_heap_hi()) {
    fp = (size_t *)((char *)bp + (bp->size & ~1) - SIZE_T_SIZE);
    printf("%s block at %p, size (in header) %d, size (in footer) %d\n",
	   (bp->size&1) ? "allocated" : "free",
	   bp,
	   (int)(bp->size),
	   (int)*fp);
    bp = (blockHdr *)((char*)bp + (bp->size & ~1));
  }
}

int in_free_list(blockHdr *fbp){
  blockHdr *p = mem_heap_lo();
  if(fbp == p)
    return 1;
  for(p = p->next_p; p != (blockHdr *) mem_heap_lo(); p = p->next_p){
    if (p == fbp)
      return 1;
  }
  return 0;
}


void *find_fit(size_t size)
{
  //assume size is already aligned
  blockHdr *p;
  for(p = ((blockHdr *)mem_heap_lo())->next_p;
      p != mem_heap_lo()&& p->size < size;
      p = p->next_p);
  if(p!=mem_heap_lo())
    return p;
  return NULL;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{

  int power = 1;
  while(power < (int)size && power < 1024)
    power*=2;

  int newsize = ALIGN(BLK_HDR_SIZE + (power > size ? power : size) + SIZE_T_SIZE);

  size_t oldsize;
  blockHdr *bp = find_fit(newsize);
  size_t *fp;
  if(bp == NULL)
    {
      bp = mem_sbrk(newsize);
      if((long)bp == -1)
	return NULL;
      else
	{
	  bp->size = newsize | 1;
	  // setting location of the foot pointer
	  fp = (size_t *) ((char *)bp + newsize - SIZE_T_SIZE);
	  // saving the size and allocation bit at the footer location
	  *fp = newsize | 1;
	}
    }
  else if(bp->size - newsize > MIN_BLK_SIZE)
    {
      remove_blk(bp);

      oldsize = bp->size;
      bp->size = newsize | 1;
      // setting location of the foot pointer
      fp = (size_t *) ((char *)bp + newsize - SIZE_T_SIZE);
      // saving the size and allocation bit at the footer location
      *fp = newsize | 1;

      blockHdr *newbp = (blockHdr *)((char *)bp + newsize);
      newbp->size = oldsize - newsize;

      insert_blk(newbp);

      size_t *newfp = (size_t *)((char*)newbp + newbp->size - SIZE_T_SIZE);
      *newfp = newbp->size;
      //newbp = coalesce(newbp); 
    }
  else
    {
   
      //setting bit as allocated
        remove_blk(bp);
       //setting location on the footer
      fp = (size_t *) ((char *)bp + bp->size- SIZE_T_SIZE);
      *fp = *fp | 1;
      bp->size |= 1;
    }
  return (char*)bp + BLK_HDR_SIZE;

}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
  size_t *fp;
  
  blockHdr *bp = ptr - BLK_HDR_SIZE;
   bp->size = bp->size & ~1L;
  fp = (size_t *) ((char *)bp + bp->size - SIZE_T_SIZE);
  *fp = *fp & ~1L; //last bit to zero
  bp = (blockHdr *)coalesce(bp);
}

void remove_blk(blockHdr *bp) {
  bp->next_p->prior_p = bp->prior_p;
  bp->prior_p->next_p = bp->next_p;
}

void insert_blk(blockHdr *bp) {
  blockHdr *head = mem_heap_lo();
  bp->next_p          = head->next_p;
  bp->prior_p         = head;
  head->next_p        = bp;
  bp->next_p->prior_p = bp;
}

void *coalesce(blockHdr *bp)
{ 

  int pr, nx;
  blockHdr *head = mem_heap_lo();
  size_t *prev_fp = (size_t *)((char*)bp - SIZE_T_SIZE);
  blockHdr *prev =(blockHdr *)( (char *)bp - (*prev_fp & ~1L) );
  blockHdr *next = (blockHdr *)((char *)bp + bp->size);
  if((void *)((char *)head + head->size) < (void *) prev)
    {
      pr = prev->size & 1;
    }
  else
    pr = 1;
  if((char *)mem_heap_hi() > (char *)next)
    {
      nx = next->size & 1;
    }
  else
    nx = 1;
  size_t *fp;

  //previous surrounding block is free
  if(!(pr) && (nx))
    { 
      prev->size += bp->size;
      fp = (size_t *) ((char *)bp + bp->size - SIZE_T_SIZE);
      *fp = prev->size & ~1L;
      return (void *)prev;
    }
    
  //next block is unsused
  else if((pr) && !(nx))
    {
      bp->size += next->size;

      remove_blk(next);
      insert_blk(bp);


      fp = (size_t *) ((char *)bp + bp->size - SIZE_T_SIZE);
      *fp = bp->size & ~1L;

      return (void *)bp;
    }
   
  //neighboring blocks are free
  else if(!(pr) && !(nx))
    {
      prev->size = prev->size + bp->size + next->size;
      remove_blk(next);
      fp = (size_t *)((char *)prev + prev->size - SIZE_T_SIZE);
      *fp = prev->size;

      return (void *)prev;

    }
  //surrounding blocks aren't free
  
  else 
    {
      insert_blk(bp);
      fp = (size_t *) ((char *)bp + bp->size - SIZE_T_SIZE);

      return (void *)bp;
    }
}


/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{

  int nx, newsize = ALIGN(BLK_HDR_SIZE +  size + SIZE_T_SIZE);
  blockHdr *bp = ptr - BLK_HDR_SIZE;
  blockHdr *p;
  size_t *fp;
  size_t increased_size;

  blockHdr *next = (blockHdr *)((char *)bp + (bp->size & ~1L));

  if((char *)mem_heap_hi() > (char *)next)
    {
      nx = next->size & 1;
    }
  else
    nx = 1;

  if(ptr == NULL)
    return mm_malloc(size);

  else if(size == 0)
    {
      mm_free(ptr);
      return NULL;
    }
  
  else if(newsize < (bp->size & ~1L))
    return ptr;
  
  else if(!nx && ((next->size& ~1L) + (bp->size& ~1L)) >= newsize)
    {
      remove_blk(next);
      bp->size += next->size;
      fp = FTPR(bp);
      *fp = bp->size;
       
      return ptr;
    }
  
    
  else if((char *)mem_heap_hi() < ((char *)next))
    {
      increased_size = newsize - (bp->size & ~1L);
      p = mem_sbrk(increased_size);
      bp->size = newsize | 1;
      fp = FTPR(bp);
      *fp = bp->size;
      return ptr;
    }
  
  else
    {

      void *newptr = mm_malloc(size);
      if(newptr == NULL)
	return NULL;
      int copySize = (bp->size & ~1L) - BLK_HDR_SIZE - SIZE_T_SIZE;
      if(size < copySize)
	copySize = size;
      memcpy(newptr,ptr,copySize);
      mm_free(ptr);
      return newptr;
    }
  
}




