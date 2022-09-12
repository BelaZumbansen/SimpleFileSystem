#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "disk_emu.h"
#include "sfs_api.h"

int BLOCK_SIZE = 1024;
int NUM_BLOCKS = 1024;
int MAX_FILE_SIZE = 32768;
int ROOT_DIRECTORY_SIZE = 4;
int I_NODE_TABLE_LENGTH = 18;
int MAX_FILE_NAME_LENGTH = 20;
int SUPER_BLOCK_LOCATION = 0;
int DIRECTORY_LOCATION = 1;
int I_NODE_TABLE_LOCATION = 5;
int FREE_BIT_MAP_LOCATION = 1023;
int FIRST_DATA_BLOCK = 23;

struct Directory_Entry {
    char file_name[20];
    int available;
    int i_node_number;
};

struct Directory_Table {
    int cur_location;
    struct Directory_Entry entries[128];
};

struct Free_Bit_Map {
    int free_block[1024];
};

struct I_Node {
    int size;
    int rw_pointer;
    int pointer[32];
};

struct I_Node_Table {
    struct I_Node i_node[128];
};

struct SFS_File {
    int mode;
    char file_name[20];
    int i_node_location;
};

struct SFS_File_Descriptor_Table {
    int open_ndx;
    struct SFS_File* file[128];
};

struct Super_Block {
    int block_size;
    int fs_size;
    int i_node_tbl_length;
    int root_directory;
};

struct Super_Block* super_block;
struct Free_Bit_Map* free_bit_map;
struct Directory_Table* directory_table;
struct I_Node_Table* i_node_table;
struct SFS_File* file[128];

/**
 * Register that a number of blocks are now being used to store data and
 * are no longer free
 */
void register_block_occupation(int start_address, int nblocks) {

    int i;
    
    for (i = start_address; i < (start_address + nblocks); i++) {
        free_bit_map->free_block[i] = 0;
    }
}

/**
 * Register that a number of blocks are now no longer in use and may once
 * again be allocated to store data
 */
void register_block_deallocation(int start_address, int nblocks) {

    int i;

    for (i = start_address; i < (start_address + nblocks); i++) {
        free_bit_map->free_block[i] = 1;
    }
}

/**
 * Retrieve the next free data block on disk
 */
int next_free_data_block() {

    int i;
    for (i = FIRST_DATA_BLOCK; i < FREE_BIT_MAP_LOCATION; i++) {
        if (free_bit_map->free_block[i]) {
            return i;
        }
    }

    return -1;
}

int sfs_getnextfilename(char *fname) {

    int i;
    for (i = directory_table->cur_location; i < 128; i++) {

        if (directory_table->entries[i].available == 0) {
            strcpy(fname, directory_table->entries[i].file_name);
            directory_table->cur_location = i + 1;
            return 1;
        }
    }

    return 0;
}

int sfs_getfilesize(const char* path) {

    int i, location;

    for (i = 0; i < 128; i++) {

        if (file[i] != NULL) {

            if (strcmp(file[i]->file_name, path) == 0) {
                location = file[i]->i_node_location;
                return i_node_table->i_node[location].size;
            }
        } 
    }

    return 0;
}

int sfs_fopen(char *name) {

    int i, j, loc;

    loc = -1;

    if (strlen(name) > MAX_FILE_NAME_LENGTH) {
        return -1;
    }

    for (i = 0; i < 128; i++) {

        if (file[i] != NULL) {

            if (strcmp(file[i]->file_name, name) == 0) {
                file[i]->mode = 1;
                return i;
            }
        }
    }


    for (i = 0; i < 128; i++) {

        if (i_node_table->i_node[i].size == -1) {
            
            i_node_table->i_node[i].size = 0;
            i_node_table->i_node[i].rw_pointer = 0;

            for (j = 0; j < 32; j++) {
                i_node_table->i_node[i].pointer[j] = -1;
            }
              
            loc = i;
            break;
        }
    }

    if (loc == -1) {
        return -1;
    }

    for (i = 0; i < 128; i++) {

        if (directory_table->entries[i].available == 1) {
            directory_table->entries[i].available = 0;
            strcpy(directory_table->entries[i].file_name, name);
            directory_table->entries[i].i_node_number = loc;
            break;
        }
    }

    for (i = 0; i < 128; i++) {

        if (file[i] == NULL) {
            
            file[i] = (struct SFS_File*)malloc(sizeof(struct SFS_File));
            file[i]->mode = 1;
            file[i]->i_node_location = loc;
            strcpy(file[i]->file_name, name);
            return i; 
        }
    }

    return -1;
}

int sfs_fread(int fileId, char *buf, int length) {

    int i_node_loc, block_num, loc_in_block, remaining_length, i;
    char temp[1024];
    struct I_Node* i_node;
    int read = 0;

    if (file[fileId] == NULL) {
        fprintf(stderr, "File does not exist");
        return -1;
    }

    if (file[fileId]->mode != 1) {
        //fprintf(stderr, "Requested File is not Open");
        return -1;
    }

    i_node_loc = file[fileId]->i_node_location;

    if (i_node_loc == -1) {
        printf("I Node for File does not Exist");
        return -1;
    }

    i_node = &i_node_table->i_node[i_node_loc];

    block_num = i_node->rw_pointer / BLOCK_SIZE;
    loc_in_block = i_node->rw_pointer % BLOCK_SIZE;
    remaining_length = length;
    
    read_blocks(i_node->pointer[block_num], 1, (void *) temp);

    for (i = 0; i < remaining_length; i++) {

        if (loc_in_block == BLOCK_SIZE) {

            block_num++;
            loc_in_block = 0;

            if (i_node->pointer[block_num] == -1) {
                buf[i] = '\0';
                break; 
            } else {
                read_blocks(i_node->pointer[block_num], 1, (void *) temp);
            }
    
        }

        if ( i_node->rw_pointer > MAX_FILE_SIZE) {
            buf[i] = '\0';
            break;
        }

        if (i_node->rw_pointer >= i_node->size) {
            buf[i] = '\0';
            break;
        }

        buf[i] = temp[loc_in_block];
        loc_in_block++;
        i_node->rw_pointer++;
        read++;
    }

    return read; 
}

int sfs_fwrite(int fileId, const char *buf, int length) {

    int i_node_loc, block_num, loc_in_block, i;
    char temp[1024];
    struct I_Node* i_node;

    if (file[fileId] == NULL) {
        fprintf(stderr, "File does not exist");
    }

    if (file[fileId]->mode != 1) {
        //fprintf(stderr, "Requested File is not Open"); 
        return -1;
    }

    i_node_loc = file[fileId]->i_node_location;

    if (i_node_loc == -1) {
        fprintf(stderr, "I node doesn't exist");
        return -1;
    }

    i_node = &i_node_table->i_node[i_node_loc];

    // If we are at the end of the maximum file size
    if ((i_node->rw_pointer + 1) >= MAX_FILE_SIZE) {
        return -1;
    }

    // If writing the whole block would put us over file size limit, write less
    if ((i_node->rw_pointer + length) >= MAX_FILE_SIZE) {
        length = MAX_FILE_SIZE - i_node->rw_pointer - 1;
    } 

    // Retrieve the location of the pointer on the disk
    block_num = i_node->rw_pointer / BLOCK_SIZE;
    loc_in_block = i_node->rw_pointer % BLOCK_SIZE;

    if (i_node->pointer[block_num] == -1) {
        i_node->pointer[block_num] = next_free_data_block();
        register_block_occupation(i_node->pointer[block_num], 1);
        temp[0] = '\0';

        if (i_node->pointer[block_num] == -1) {
            fprintf(stderr, "Failed to allocate data block.");
            return -1;
        }
    } else {
        read_blocks(i_node->pointer[block_num], 1, (void *) temp);
    }

    // Write the buffer
    for (i = 0; i < length; i++) {

        if (loc_in_block == BLOCK_SIZE) {

            write_blocks(i_node->pointer[block_num], 1, (void *) temp);

            block_num++;
            loc_in_block = 0;
            temp[0] = '\0';
            
            if (i_node->pointer[block_num] != -1) {
               read_blocks(i_node->pointer[block_num], 1, (void *) temp); 
            } else {
                i_node->pointer[block_num] = next_free_data_block();
                register_block_occupation(i_node->pointer[block_num], 1);

                if (i_node->pointer[block_num] == -1) {
                    fprintf(stderr, "Failed to allocate data block.");
                    return -1;
                }
            }
        } 

        temp[loc_in_block] = buf[i];
        loc_in_block++;
        i_node->rw_pointer++;

        if (i_node->rw_pointer >= MAX_FILE_SIZE) {
            break;
        }
    }

    // Ensure that we null terminate our data on disk so that we don't run into issues when reading it
    if (loc_in_block == BLOCK_SIZE) {

        write_blocks(i_node->pointer[block_num], 1, (void *) temp);

        block_num++;

        if (i_node->pointer[block_num] == -1) {

            i_node->pointer[block_num] = next_free_data_block();
            register_block_occupation(i_node->pointer[block_num], 1);

            if (i_node->pointer[block_num] == -1) {
                fprintf(stderr, "Failed to allocate data block.");
                return -1;
            }

            temp[0] = '\0';
            write_blocks(i_node->pointer[block_num], 1, (void *) temp);
        }
    } else {

        if (i_node->rw_pointer > i_node->size) {
            temp[loc_in_block] = '\0';
        }

        write_blocks(i_node->pointer[block_num], 1, (void *) temp);
    }

    if (i_node->rw_pointer > i_node->size) {
        i_node->size = i_node->rw_pointer;
    }

    return length;
}

void save_cache_to_disk() {

    write_blocks(FREE_BIT_MAP_LOCATION, 1, (void *) free_bit_map);
    write_blocks(SUPER_BLOCK_LOCATION, 1, (void *) super_block);
    write_blocks(DIRECTORY_LOCATION, ROOT_DIRECTORY_SIZE, (void *) directory_table);
    write_blocks(I_NODE_TABLE_LOCATION, I_NODE_TABLE_LENGTH, (void *) i_node_table);
}

int sfs_fseek(int fileId, int loc) {

    if (file[fileId] == NULL) {
        fprintf(stderr, "File does not exist");
    }

    int location = file[fileId]->i_node_location; 

    if (location != -1) {

        if (loc < i_node_table->i_node[location].size && loc >= 0) {
            i_node_table->i_node[location].rw_pointer = loc;
            return 0;
        } else {
            return -1;
        }
    } else {
        return -1;
    }
}

int sfs_fclose(int fileId) {

    save_cache_to_disk();

    if (file[fileId] == NULL) {
        fprintf(stderr, "File to delete does not exist");
        return -1;
    } else if (file[fileId]->mode == 0) {
        return -1;
    }

    file[fileId]->mode = 0;
    return 0;
}

int sfs_remove(char *inputfile) {

   int i, i_node_location, pointer_loc;
   i_node_location = -1;

    // Find file in file descriptor array
   for (i = 0; i < 128; i++) {

       if (file[i] != NULL) {

        if (strcmp(file[i]->file_name, inputfile) == 0) {
           
           free(((struct SFS_File*)file[i]));
           file[i] = NULL;
        break;
        }
       }
   } 

   for (i = 0; i < 128; i++) {

       if (strcmp(directory_table->entries[i].file_name, inputfile) == 0) {
           directory_table->entries[i].available = 1;
           i_node_location = directory_table->entries[i].i_node_number;
           break;
       }
   }

   if (i_node_location == -1) {
       return 1;
   }

   if (i_node_table->i_node[i_node_location].size != -1) {

       for (i = 0; i < 32; i++) {

           pointer_loc = i_node_table->i_node[i_node_location].pointer[i];
           if (pointer_loc != -1) {
               register_block_deallocation(pointer_loc, 1);
           }
       }
       i_node_table->i_node[i_node_location].size = -1;
   } else {
       return 1;
   }

   return 0;
}

/**
 * Allocate memory space for the cached I Node Table and register where it will
 * be on disk
 */
void init_i_node_table() {

    int i;
    i_node_table = (struct I_Node_Table*)malloc(sizeof(struct I_Node_Table));

    for (i = 0; i < 128; i++) {
        i_node_table->i_node[i].size = -1;
    }

    write_blocks(I_NODE_TABLE_LOCATION, I_NODE_TABLE_LENGTH, (void *) i_node_table);
    register_block_occupation(I_NODE_TABLE_LOCATION, I_NODE_TABLE_LENGTH);
}

/**
 * Allocate memory space for the cached Directory table and register where it will
 * be on disk
 */
void init_directory_table() {

    int i;
    directory_table = (struct Directory_Table*)malloc(sizeof(struct Directory_Table));

    for (i = 0; i < 128; i++) {
        directory_table->entries[i].available = 1;
    }

    directory_table->cur_location = 0;
    write_blocks(DIRECTORY_LOCATION, ROOT_DIRECTORY_SIZE, (void *) directory_table);
    register_block_occupation(DIRECTORY_LOCATION, ROOT_DIRECTORY_SIZE);
}

/**
 * Initialize the pointers of the file descriptor table
 */
void init_fd_table() {

    for (int i = 0; i < 128; i++) {
        file[i] = NULL;
    }
}

/**
 * Initialize the free bit map to correspond with a completely free disk
 * aside from its own location on disk
 */
void init_free_bit_map() {

    int i;
    free_bit_map = (struct Free_Bit_Map*)malloc(sizeof(struct Free_Bit_Map));

    for (i = 0; i<1023; i++) {
        free_bit_map->free_block[i] = 1;
    }

    free_bit_map->free_block[1023] = 0;
}

/**
 * Initialize the super block of the file system
 */
void init_super_block() {

    super_block = (struct Super_Block*)malloc(sizeof(struct Super_Block));
    super_block->block_size = BLOCK_SIZE;
    super_block->fs_size = NUM_BLOCKS;
    super_block->i_node_tbl_length = I_NODE_TABLE_LENGTH;
    super_block->root_directory = DIRECTORY_LOCATION;
    write_blocks(SUPER_BLOCK_LOCATION, 1, (void *) super_block);
    register_block_occupation(SUPER_BLOCK_LOCATION, 1);
}

/**
 * Reestablish the file descriptor table from the directory entries
 */
void fill_fd_table() {

    int i, j;
    j = 0;
    for (i = 0; i < 128; i++) {

        if (directory_table->entries[i].available == 0) {

            if (file[j] != NULL) {
                free(file[j]);
            }
            file[j] = (struct SFS_File*)malloc(sizeof(struct SFS_File));
            strcpy(file[j]->file_name, directory_table->entries[i].file_name);
            file[j]->i_node_location = directory_table->entries[i].i_node_number;
            j++;
        }
    }

    while (j < 128) {
        if (file[j] != NULL) {
            free(file[j]);
        }
        file[j] = NULL;
        j++;
    }
}

void mksfs(int fresh) {

    if (fresh) {
        init_fresh_disk("sfs.file", BLOCK_SIZE, NUM_BLOCKS);
        init_free_bit_map();
        init_super_block();
        init_fd_table();
        init_i_node_table();
        init_directory_table();
        write_blocks(FREE_BIT_MAP_LOCATION, 1, (void *) free_bit_map);
    } else {
        
        init_disk("sfs.file", BLOCK_SIZE, NUM_BLOCKS);

        // Free previous cached data structures
        if (super_block != NULL) {
            free(super_block);
            free(directory_table);
            free(i_node_table);
            free(free_bit_map);
        }

        // Alocate memory for cached structures
        super_block     = (struct Super_Block*)malloc(sizeof(struct Super_Block));
        directory_table = (struct Directory_Table*)malloc(sizeof(struct Directory_Table));
        i_node_table    = (struct I_Node_Table*)malloc(sizeof(struct I_Node_Table));
        free_bit_map    = (struct Free_Bit_Map*)malloc(sizeof(struct Free_Bit_Map));

        // Retrieve data structures from disk
        read_blocks(SUPER_BLOCK_LOCATION, 1, (void *) super_block);
        read_blocks(DIRECTORY_LOCATION, ROOT_DIRECTORY_SIZE, (void *) directory_table);
        read_blocks(I_NODE_TABLE_LOCATION, I_NODE_TABLE_LENGTH, (void *) i_node_table);
        read_blocks(FREE_BIT_MAP_LOCATION, 1, (void *) free_bit_map);
        fill_fd_table();
    }
}