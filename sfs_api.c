/*
file system code
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disk_emu.h"
#include "sfs_api.h"
#include <stdint.h>
#define FILE_NAME_LIMIT 32
#define MAXFILENAME 32
#define BLK_SIZE 1024
#define NUM_BLKS 2048
#define NUM_INODES 160
#define NUM_INO_BLKS (sizeof(inode_t)*NUM_INODES/BLK_SIZE + 1)
#define NUM_BITMAP_BLKS (sizeof(bitmap_t)/BLK_SIZE + 1)
#define NUM_DIR_BLKS 11
#define NUM_DIR_FDT SIZE_DIR/sizeof(dir_entry_t)
#define SIZE_DIR NUM_DIR_BLKS*BLK_SIZE

/*
Used structures
*/
//block type
typedef struct _block_t {
    char data[BLK_SIZE];
} block_t;


//directory entry 
typedef struct _dir_entry_t {
    char filename[64-sizeof(int)*2];
    int ava;
    int inode;
} dir_entry_t;

//super block
typedef struct _super_block_t{
    uint64_t magic;
    uint64_t block_size;
    uint64_t fs_size;
    uint64_t inode_table_len;
    uint64_t root_dir_inode;
} super_block_t;
//structure for indirect pointer
typedef struct {
    int ind_pts[BLK_SIZE/4];
} indirect_t;
//inode
typedef struct _inode_t{
    int mode;
    int link_cnt; //1 is used, 0 is not used
    int size;
    int ptrs[12];
    int ind_ptrs;
} inode_t;

//ofdt entries
typedef struct _oftd_entry_t{
    uint32_t ava;
    uint32_t inode;
    uint32_t rw_ptr; // offset
} oftd_entry_t;

//bitmap
typedef struct _bitmap_t{
    char map[NUM_BLKS];
} bitmap_t;

//variables
int current_file_ind;
super_block_t supblk;
inode_t itable[NUM_INODES];
bitmap_t bitmap;
dir_entry_t directory[NUM_DIR_FDT];
oftd_entry_t fdt[NUM_DIR_FDT];
block_t dirmem[NUM_DIR_BLKS];
dir_entry_t *dir = (dir_entry_t*) dirmem;
indirect_t indirect_ptr;
char temp[BLK_SIZE];

//update directories and tables
void update_tables(){
        
        write_blocks(0, 1, (void*)&supblk);//write super block
        write_blocks(1,NUM_INO_BLKS,(void*)itable);//write i node table
        write_blocks(1+NUM_INO_BLKS, NUM_BITMAP_BLKS, (void*)&bitmap);//write bitmap
        //write directory with NUM_DIR_BLKS blocks
        write_blocks(1+NUM_INO_BLKS+NUM_BITMAP_BLKS, NUM_DIR_BLKS, (void*)dirmem);
        return;
}

void reset_inode(int inode_ind){
    itable[inode_ind].size = 0;
    itable[inode_ind].link_cnt = 0;
    for(int i = 0; i<12;i++){
        itable[inode_ind].ptrs[i] = -1;
    } 
    itable[inode_ind].ind_ptrs = -1; 
}
//returns bit map index of a free block
int ffree_block(){
    for(int j = 0; j<NUM_BLKS;j++){
        if(bitmap.map[j] == '1'){
            return j;
        }
    }  
}
//convert to disk num, also set up disk block by setting free map
int convert_blk_to_disk(int inode_index, int blk_num){
    //if block less than 12, look in direct pointers
    //check if blk_num out of max file range
    if(blk_num>(12+BLK_SIZE/4 -1 )){
        
        return -1;
    }
    if(blk_num<12){
        //if block not allocated, find free block and assign
        if(itable[inode_index].ptrs[blk_num]==-1){
            int bit_ind = ffree_block();
            //assign bitmap
            bitmap.map[bit_ind] = '0';
            //assign inode
            itable[inode_index].ptrs[blk_num] = bit_ind;
            //update table
            update_tables();
            return bit_ind;
        }else{
            //if block is allocated return pointer
            return itable[inode_index].ptrs[blk_num];
        }
    }
    //else block num => 12 look in indirect pointers
    else{
        //check if indirect block is setup
        //if not, setup indirect block 
        if(itable[inode_index].ind_ptrs==-1){
            int indirect_bit_ind = ffree_block();
            //assign bitmap
            bitmap.map[indirect_bit_ind] = '0';
            //assign to inode
            itable[inode_index].ind_ptrs = indirect_bit_ind;
            //read in indirect pointers block and reset all pointers
            read_blocks(itable[inode_index].ind_ptrs,1,(void*)&indirect_ptr);
            for(int i = 0; i< BLK_SIZE/4;i++){
                indirect_ptr.ind_pts[i] = -1;
            }
            //find free block to assign to a indirect ptr and
            int indirect_pt = ffree_block();
            //assign bitmap
            bitmap.map[indirect_pt] = '0';
            //assign to first indirect pt
            indirect_ptr.ind_pts[0] = indirect_pt;
            //update disk
            write_blocks(itable[inode_index].ind_ptrs,1,(void*)&indirect_ptr);
            update_tables();
            return indirect_pt;
        }//else, indirect block is setup
        else{
            //read in indirect block
            read_blocks(itable[inode_index].ind_ptrs,1,(void*)&indirect_ptr);
            int indirect_num_blk = blk_num-12;
            //check if data block is allocated
            if(indirect_ptr.ind_pts[indirect_num_blk] ==-1){
                //if not, find free block
                int ind_ptr = ffree_block(); 
                //set in bit map
                bitmap.map[ind_ptr] = '0';
                //set in indirect block
                indirect_ptr.ind_pts[indirect_num_blk] = ind_ptr;
                //update indirect block
                write_blocks(itable[inode_index].ind_ptrs,1,(void*)&indirect_ptr);
                //return disk num
                return ind_ptr;
            }else{
                //if data block is allocated
                //return disk num of block
                int ind_ptr = indirect_ptr.ind_pts[indirect_num_blk];
                return ind_ptr;
            }
        }
    }
}
void mksfs(int fresh){
    if(fresh){
        
        
        init_fresh_disk("CZ.sfs", BLK_SIZE, NUM_BLKS);
        //initiate super blocks, bitmap, itable
        //super block
        supblk.magic = 0xACBD0005;
        supblk.block_size = BLK_SIZE;
        supblk.fs_size = NUM_BLKS*BLK_SIZE;
        supblk.inode_table_len = NUM_INODES;
        supblk.root_dir_inode = 0;
        //reset dir
        for(int n = 0; n<NUM_DIR_FDT;n++){
            dir[n].ava = 1;
            dir[n].inode = -1;
            strcpy(dir[n].filename , "");
        }
        //set bitmap
        //set the first block(super block) + number of blocks for
        //inodes + blocks for bitmap + directory in data blocks to 0 as they are used
        for(int i = 0; i<1+NUM_INO_BLKS+NUM_BITMAP_BLKS+NUM_DIR_BLKS+1;i++){
            bitmap.map[i] = '0';
        }
        //set rest to 1 indicate unused
        for(int j = 1+NUM_INO_BLKS+NUM_BITMAP_BLKS+NUM_DIR_BLKS+1; j<NUM_BLKS;j++){
            bitmap.map[j] = '1';
        }

        
        //reset inodes
        for (int i = 0; i<NUM_INODES;i++){
            reset_inode(i);
        }
        current_file_ind = 0;
        //set root inode
        itable[0].mode = current_file_ind;
        itable[0].link_cnt = 1;
        itable[0].size = 0;
        itable[0].ptrs[0] = NUM_INO_BLKS+1+NUM_BITMAP_BLKS; //super block plus blocks for inodes and bitmap
        //write tables to disks
        update_tables();
    }
    //else: not fresh so dont format disk
    else{
        
        init_disk("CZA3.sfs", BLK_SIZE, NUM_BLKS);
        //load using same procedure as write
        read_blocks(0, 1, (void*)&supblk);
        read_blocks(1, NUM_INO_BLKS, (void*)itable);
        read_blocks(1+NUM_INO_BLKS, NUM_BITMAP_BLKS, (void*)&bitmap);
        read_blocks(1+NUM_INO_BLKS+NUM_BITMAP_BLKS,NUM_DIR_BLKS,(void*)dirmem);
        current_file_ind = itable[0].mode;

    }
    for(int l = 0; l<NUM_DIR_FDT;l++){
        fdt[l].ava = 1;     
    }
    return;
}
int sfs_getnextfilename(char* filename){
    int cur_ind = itable[0].mode;
    for( cur_ind; cur_ind<NUM_DIR_FDT;cur_ind++){
        //if dir. ava is 0 return name and update cur index
        
        if(dir[cur_ind+1].ava ==0){
            //copy name of file
            memcpy(filename,dir[cur_ind].filename,MAXFILENAME);
            itable[0].mode = cur_ind+1;
            update_tables();
            return cur_ind+1;
        }
    }
    return 0;
}

int sfs_getfilesize(const char* path){

    int inode = file_exist(path);

    if(inode == -1){

        return -1;
    }
    return itable[inode].size;
}
//if file exist in directory return inode index
//return -1 if not found
int file_exist(char* file_name){
    
    int i;
    for(i = 0;i<NUM_DIR_FDT;i++){
        if(!strcmp(dir[i].filename, file_name)&&!dir[i].ava){
            return dir[i].inode;
        }
    }
    return -1;
}
/*
sfs_fopen opens file and returns index of newly opened file in the fdt,
 if file doesn't exist create new file and set size to 0,
if file does exist open in append mode, set file pointer to end of file(size)

*/
int sfs_fopen(char* file_name){
    //check if over file name limit(16)
    
    if(strlen(file_name) > FILE_NAME_LIMIT){
        return -1;
    }
   
    //check if it's old file
    //check if file exists in directory
    //if it exists get it's inode from directory
    int inode_ind = file_exist(file_name);
    
    if (inode_ind != -1){
        //check if file already in fdt by checking if inode ind exist in fdt if it does return -1
        for(int p = 0; p<NUM_DIR_FDT; p++){
            if(fdt[p].inode==inode_ind){
                return p;
            }
        }
        //make entry in fdt and return index in fdt
        //get size from inode table
        int size = itable[inode_ind].size;
        for(int k = 0; k<NUM_DIR_FDT; k++){
            
            if(fdt[k].ava){
                fdt[k].ava = 0;
                fdt[k].rw_ptr = size;
                fdt[k].inode = inode_ind;
                
                return k;
            }
        }
        //if failed cuz max entries in fdt return -1
        return -1;
    }else{
        //else, it's new file, go to inode table and find free inode, create entry(countlk = 1,size = 0)
        int s;
        for(s = 0;s<NUM_INODES;s++){
            if(itable[s].link_cnt == 0){
                reset_inode(s);
                itable[s].link_cnt = 1;

                break;
            }
        }
        //if s = inode index < num inodes
        if(s<NUM_INODES){
            //create directory entry in free slot with inode index and file name
           
            int t;
            for(t = 0; t<NUM_DIR_FDT;t++){
                //printf("ava is %d for iteration %d\n",dir[t].ava,t);
                if(dir[t].ava == 1){
                    //printf("t in loop %d\n",t);
                    dir[t].ava = 0;
                    strcpy(dir[t].filename,file_name);
                    dir[t].inode = s; 
                    break;
                }
            }
            
            //create entry in fdt with inode index and offset = 0
            int f;
            for(f = 0; f<NUM_DIR_FDT;f++){
                if(fdt[f].ava==1){
                    fdt[f].ava = 0;
                    fdt[f].inode = s;
                    fdt[f].rw_ptr = 0;
                    break;
                }
            }
            
            //return fdt index
            update_tables();
            return f;
        }else{
            return -1;
        }
    }
}

int sfs_fclose(int in){
    if(fdt[in].ava == 0){
        fdt[in].ava = 1;
        fdt[in].inode = 0;
        fdt[in].rw_ptr = 0;
        return 0;
    }
    return -1;
}
int sfs_fwrite(int fileID, const char *buf, int length){
    //check if fileid exist in fdt
    
    if(fdt[fileID].ava){
        return -1;
    }
    //current location 
    int cloc = fdt[fileID].rw_ptr;
    //array to hold block read in
    //char temp[BLK_SIZE];
    //char temp1[BLK_SIZE];
    
    length = length ;
    int len_left = length;
    int len_to_write;
    int blk_num = cloc/BLK_SIZE;
    int loc_in_blk = (cloc%BLK_SIZE);
    int disk_num;
    int sz; //variable to keep track of size
    disk_num = convert_blk_to_disk(fdt[fileID].inode, blk_num);
        //if disk_num = -1, max file size reached
        if(disk_num == -1){
            return disk_num;
        }
    int buf_copied = 0;//variable to keep track of how much of buffer has been copied
    // while length to write > 0, read in block and write
    while(len_left>0){
        //find disk number
        disk_num = convert_blk_to_disk(fdt[fileID].inode, blk_num);
        //if disk_num = -1, max file size reached
        if(disk_num == -1){
            
            sz = (length-len_left) - (itable[fdt[fileID].inode].size-cloc);
            itable[fdt[fileID].inode].size=itable[fdt[fileID].inode].size+sz;
            //update rw_ptr
            fdt[fileID].rw_ptr = itable[fdt[fileID].inode].size;
            
            update_tables();
            return length-len_left;
        }
        
        //read in block
        read_blocks(disk_num, 1,(void*) temp);
       
        //check how much to copy into block, if len_left> (blocksize-loc_in_blk)
        //len_to_write = blocksize - loc_in_blk
        //length left = length left - len_to_write
        if(len_left>(BLK_SIZE-loc_in_blk)){
            len_to_write = BLK_SIZE-loc_in_blk;
            len_left = len_left - len_to_write;
        }//else if len left < blocksize-loc_in_blk
        //length to write = len_left
        else if(len_left<(BLK_SIZE-loc_in_blk)){
            len_to_write = len_left;
            len_left = 0;
        }
        
        //write using memcpy to temp
       

        int s = temp+loc_in_blk;
        int b = buf+buf_copied;
      
        memcpy(temp+loc_in_blk,buf+buf_copied,len_to_write);
       
        //write temp to disk
        write_blocks(disk_num,1,(void*) temp);
       
        //if length left >0, blk_num +1, loc_in_blk = 0, buf copied = buf_copied+len_to_write
        if(len_left>0){
            blk_num = blk_num+1;
            loc_in_blk = 0;
            buf_copied = buf_copied+len_to_write;
        }
    }
    //update size
    sz = length - (itable[fdt[fileID].inode].size-cloc);
    itable[fdt[fileID].inode].size=itable[fdt[fileID].inode].size+sz;
    //update rw_ptr
    fdt[fileID].rw_ptr = itable[fdt[fileID].inode].size;
    
    update_tables();
    //read_blocks(disk_num,1,temp1);
    
    return length;
}


int sfs_fread(int fileID, char *buf, int length){
    //check if file exists in fdt
    if(fdt[fileID].ava){
        return -1;
    }
    //get current rw loc
    int cloc = fdt[fileID].rw_ptr;
    //array to hold block read in
    
    if(length>itable[fdt[fileID].inode].size){
        length = itable[fdt[fileID].inode].size;
    }
    //char temp1[BLK_SIZE];
    int len_left = length;
    int len_to_read;
    int blk_num = cloc/BLK_SIZE;
    int loc_in_blk = cloc%BLK_SIZE;
    int disk_num;
    int buf_copied = 0;
    int length_read;
    disk_num = convert_blk_to_disk(fdt[fileID].inode, blk_num);
    //if max reached return
    if(disk_num == -1){
        return disk_num;
    }
    //read_blocks(1,1,temp);
    
    //while len_left>0 get block and read to buf
    while(len_left>0){
        //find disk number
        disk_num = convert_blk_to_disk(fdt[fileID].inode, blk_num);
        
        //if max reached return
        if(disk_num == -1){
            fdt[fileID].rw_ptr = fdt[fileID].rw_ptr+(length-len_left);
            return length-len_left;
        }
        
        //read in block
        read_blocks(disk_num,1,temp);
       

        //check how much to copy into buf, if len_left> (blocksize-loc_in_blk)
        //len_to_read = blocksize - loc_in_blk
        //length left = length left - len_to_read
        if(len_left>=(BLK_SIZE-loc_in_blk)){
            len_to_read = BLK_SIZE-loc_in_blk;
            len_left = len_left - len_to_read;
        }//else if len left < blocksize-loc_in_blk
        //length to read = len_left
        else if(len_left<(BLK_SIZE-loc_in_blk)){
            len_to_read = len_left;
            len_left = 0;
        }
        memcpy(buf+buf_copied, temp+loc_in_blk, len_to_read);
       
        //if length left >0, blk_num +1, loc_in_blk = 0, buf copied = buf_copied+len_to_write
        
        if (len_left>0){
           
            blk_num = blk_num+1;
            buf_copied = buf_copied+ len_to_read;
            loc_in_blk = 0;
        }
        length_read += len_to_read;

    }
    //update rw_ptr
    //fdt[fileID].rw_ptr = itable[fdt[fileID].inode].size;
    fdt[fileID].rw_ptr = cloc+length;

    return length;
}

//seek to location from beginning
int sfs_fseek(int fileID, int loc){
    if(loc > itable[fdt[fileID].inode].size||fdt[fileID].inode == 0 ||fdt[fileID].ava == 1){
        return -1;
    }
    fdt[fileID].rw_ptr = loc;
    return 0;
}

//The sfs_remove()
//removes the file from the directory entry, releases the i-Node and releases the data blocks used by the file
//(i.e., the data blocks are added to the free block list/map), so that they can be used by new files in the future.
int sfs_remove(char* file_name){
    //check if file exist
    
    int inode_ind = file_exist(file_name);
    
    if(inode_ind == -1){   
        return -1;
    }
    //if exist use inode to reset directory
    int i;
    for(i = 0; i<NUM_DIR_FDT;i++){
        
        if(dir[i].ava == 0 && dir[i].inode == inode_ind){
            //reset file name
            int n;
            for(n = 0; n<FILE_NAME_LIMIT;n++){
                dir[i].filename[n] = 0;
            }
            dir[i].filename[n-1] = '\0';
            //reset ava
            dir[i].ava = 1;
            //reset inode;
            dir[i].inode = -1;
            break;
        }
    }
    // free up bit map
    for(int v = 0; v<12; v++){
        bitmap.map[itable[inode_ind].ptrs[v]] = '1';
    }
    //free up indirect bit map
    if(itable[inode_ind].ind_ptrs!=-1){
        read_blocks(itable[inode_ind].ind_ptrs,1,(void*)&indirect_ptr);
        for(int m = 0; m< BLK_SIZE/4;m++){
            if(indirect_ptr.ind_pts[m]==-1){
                continue;
            }else{
                bitmap.map[indirect_ptr.ind_pts[m]]= '1';
            }
        }
        bitmap.map[itable[inode_ind].ind_ptrs]='1';
    }
    //then reset inode table entry
    reset_inode(inode_ind);

    //reset fdt
    int r;
    
    for(r = 0;r<NUM_DIR_FDT;r++){
        
        if(fdt[r].ava == 0&&fdt[r].inode == inode_ind){
            
            fdt[r].ava = 1;
            fdt[r].inode = -1;
        }
    }
    //write to disk
    update_tables();
    return i;
}