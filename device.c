#include<linux/module.h>

#include<linux/mutex.h>

#include<linux/kernel.h>

#include<linux/init.h>

#include <linux/bvec.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>

#include <linux/bitmap.h>

#include "dedup.h"

/* This is a structure which will store  information about the underlying device 
*  Param:
* dev : underlying device
* start:  Starting sector number of the device
*/

DEFINE_MUTEX(rhashtable_lock);

struct my_dm_target {

        struct dm_dev *dev;

        sector_t start;

};

struct crypto_shash * alg;

unsigned long *bitmap;
//forced to use 32-bit hash (not even with custom hashfn can you use anything beefier than u32 for hash), and we're even limited to crc32c by the available ciphers. at least it's fast.
struct hash2data_unit {
	u32 crc32c;
	struct rhash_head linkage;
	sector_t actual_sector;

};

struct sect2hash_unit {
	sector_t sector;
	struct rhash_head linkage;
	u32 crc32c;
};

struct rhashtable rht_sect2hash, rht_hash2data;

/*rht_hashfn_t hash2data_hashfn(const void* data, u32 len, u32 seed)
{
	return ((struct hash2data_unit*)data)->crc32c;
}*/


const static struct rhashtable_params hash2data_rht_params = {
	.key_len = sizeof(u32),
	.key_offset = offsetof(struct hash2data_unit, crc32c),
	.head_offset = offsetof(struct hash2data_unit, actual_sector)
};


const static struct rhashtable_params sect2hash_rht_params = {
	.key_len = sizeof(sector_t),
	.key_offset = offsetof(struct sect2hash_unit, sector),
	.head_offset = offsetof(struct sect2hash_unit, crc32c)
};
/* This is map function of basic target. This function gets called whenever you get a new bio
 * request.The working of map function is to map a particular bio request to the underlying device. 
 *The request that we receive is submitted to out device so  bio->bi_bdev points to our device.
 * We should point to the bio-> bi_dev field to bdev of underlying device. Here in this function,
 * we can have other processing like changing sector number of bio request, splitting bio etc. 
 *
 *  Param : 
 *  ti : It is the dm_target structure representing our basic target
 *  bio : The block I/O request from upper layer
 *  map_context : Its mapping context of target.
 *
 *: Return values from target map function:
 *  DM_MAPIO_SUBMITTED :  Your target has submitted the bio request to underlying request
 *  DM_MAPIO_REMAPPED  :  Bio request is remapped, Device mapper should submit bio.  
 *  DM_MAPIO_REQUEUE   :  Some problem has happened with the mapping of bio, So 
 *                                                re queue the bio request. So the bio will be submitted 
 *                                                to the map function  
 */

static u32 crc32c = 0;
static u32 key = 20;
static int basic_target_map(struct dm_target *ti, struct bio *bio)

{


	struct my_dm_target *mdt = (struct my_dm_target *) ti->private;

	//printk("\n<<in function basic_target_map \n");

	struct bio_vec bvec;
	void *buf;
	struct bio *bio_clone;
	struct bvec_iter iter;
	void* res;
	bio_get(bio);
	bio_clone = bio_clone_fast(bio, GFP_NOIO, bio->bi_pool);

	unsigned potential_size = bio_segments(bio_clone);
	u64 unit_iter = 0;

	u64 counter = 0;

	bio_for_each_segment(bvec, bio_clone, iter)
	{
		//buf = bvec_kmap_local(&bvec);
		// do hash map stuff, bytes length (is) bvec.bv_len
		//printk("accessed block size %d", bvec.bv_len);
		if (bio_data_dir(bio) == WRITE)
		{

			//calc_hash(alg, buf, bvec.bv_len,(unsigned char*) &crc32c);

			struct sect2hash_unit  *s2h_unit = kmalloc(sizeof(struct sect2hash_unit), GFP_KERNEL);
			s2h_unit->sector	= 0xdeadbeef;
			s2h_unit->crc32c = 5;
			struct hash2data_unit *h2d_unit = kmalloc(sizeof(struct hash2data_unit), GFP_KERNEL);
			h2d_unit->crc32c = 0x0badf00d;
			h2d_unit->actual_sector = 7; //just to fill up

			res = rhashtable_lookup_get_insert_fast(&rht_sect2hash, &(s2h_unit->linkage), sect2hash_rht_params);
			if (IS_ERR(res))
			{
				printk("Failed to insert to s2h");
				return DM_MAPIO_REQUEUE;
			}
			res = rhashtable_lookup_get_insert_fast(&rht_hash2data, &(h2d_unit->linkage), hash2data_rht_params);
			if (IS_ERR(res))
			{
				printk("Failed to insert to h2d");
				return DM_MAPIO_REQUEUE;
			}
			printk("%llu", counter);				
			++counter;
		}
		else if (bio_data_dir(bio) == READ)
		{
			struct sect2hash_unit *s2h_unit = rhashtable_lookup_fast(&rht_sect2hash, &iter.bi_sector, sect2hash_rht_params);
			struct hash2data_unit *h2d_unit = rhashtable_lookup_fast(&rht_hash2data, &s2h_unit->crc32c, hash2data_rht_params);
			iter.bi_sector = h2d_unit->actual_sector;
		}
		//kunmap_local(buf);
	}

	bio->bi_bdev = mdt->dev->bdev;
	submit_bio(bio);
	bio_put(bio);
	//	rhashtable_insert_fast(&rht_sect2hash, &(s2h_added_queue[0].linkage), sect2hash_rht_params);
	//	rhashtable_insert_fast(&rht_sect2hash, &(s2h_added_queue[1].linkage), sect2hash_rht_params);
	for (unit_iter = 0; unit_iter < counter; ++unit_iter)
	{
		printk("%llu", unit_iter);
	}
	//printk("\n>>out function basic_target_map \n");       

	return DM_MAPIO_SUBMITTED;

}



/* This is Constructor Function of basic target 
 *  Constructor gets called when we create some device of type 'basic_target'.
 *  So it will get called when we execute command 'dmsetup create'
 *  This  function gets called for each device over which you want to create basic 
 *  target. Here it is just a basic target so it will take only one device so it  
 *  will get called once. 
 */



static int 
basic_target_ctr(struct dm_target *ti,unsigned int argc,char **argv)

{


        struct my_dm_target *mdt;

        unsigned long long start;



        //printk("\n >>in function basic_target_ctr \n");



        if (argc != 2) {

                //printk("\n Invalid no.of arguments.\n");

                ti->error = "Invalid argument count";

                return -EINVAL;

        }



        mdt = kmalloc(sizeof(struct my_dm_target), GFP_KERNEL);



        if(mdt==NULL)

        {

                //printk("\n Mdt is null\n");

                ti->error = "dm-basic_target: Cannot allocate linear context";

                return -ENOMEM;

        }       



        if(sscanf(argv[1], "%llu", &start)!=1)

        {

                ti->error = "dm-basic_target: Invalid device sector";

                goto bad;

        }



        mdt->start=(sector_t)start;

        

        /* dm_get_table_mode 
         * Gives out you the Permissions of device mapper table. 
         * This table is nothing but the table which gets created
         * when we execute dmsetup create. This is one of the
         * Data structure used by device mapper for keeping track of its devices.
         *
         * dm_get_device 
         * The function sets the mdt->dev field to underlying device dev structure.
         */

    


        if (dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &mdt->dev)) {

                ti->error = "dm-basic_target: Device lookup failed";

                goto bad;

        }



        ti->private = mdt;

	//printk("isize read %u", i_size_read(mdt->dev->bdev->bd_inode) / 512);

	//bitmap = bitmap_alloc(i_size_read(mdt->dev->bdev->bd_inode) / 512, GFP_KERNEL);
	
	
	struct sect2hash_unit s2h_unit = {
		.sector	= 5,
		.crc32c = 3
	};
	struct hash2data_unit h2d_unit = {
		.crc32c = 20,
		.actual_sector = 7 //just to fill up
	};

	rcu_read_lock();
	rhashtable_lookup_insert_fast(&rht_sect2hash, &s2h_unit.linkage, sect2hash_rht_params);
	rhashtable_lookup_insert_fast(&rht_hash2data, &h2d_unit.linkage, hash2data_rht_params);
	rcu_read_unlock();

	rcu_read_lock();
	rhashtable_lookup_insert_fast(&rht_sect2hash, &s2h_unit.linkage, sect2hash_rht_params);
	rhashtable_lookup_insert_fast(&rht_hash2data, &h2d_unit.linkage, hash2data_rht_params);
	rcu_read_unlock();
        //printk("\n>>out function basic_target_ctr \n");                       

        return 0;



  bad:

        kfree(mdt);

        //printk("\n>>out function basic_target_ctr with errorrrrrrrrrr \n");           

        return -EINVAL;


}





/*
 * This is destruction function
 * This gets called when we remove a device of type basic target. The function gets 
 * called per device. 
 */

static void basic_target_dtr(struct dm_target *ti)

{

        struct my_dm_target *mdt = (struct my_dm_target *) ti->private;


        //printk("\n<<in function basic_target_dtr \n");        


        dm_put_device(ti, mdt->dev);


        kfree(mdt);

        //printk("\n>>out function basic_target_dtr \n");               

}





/*

 * This structure is fops for basic target.

 */


static struct target_type basic_target = {

        

        .name = "basic_target",

        .version = {1,0,0},

        .module = THIS_MODULE,

        .ctr = basic_target_ctr,

        .dtr = basic_target_dtr,

        .map = basic_target_map


};

 
/*---------Module Functions -----------------*/



static int init_basic_target(void)

{

        int result;

	int rht_sect2hash_success, rht_hash2data_success;
	printk("%f", 1/0);
	rht_sect2hash_success = rhashtable_init(&rht_sect2hash, &sect2hash_rht_params);
	rht_hash2data_success = rhashtable_init(&rht_hash2data, &hash2data_rht_params);
        result = dm_register_target(&basic_target);

	if (rht_sect2hash_success < 0)
	{
		printk("\n failed to init rht_sect2hash");
		return rht_sect2hash_success;
	}

	if (rht_hash2data_success < 0)
	{
		printk("\n failed to init rht_hash2data");
		return rht_hash2data_success;
	}

//	dm_task_set_name(dmt, basic_target.name);

//	dm_task_set_add_node(dmt, DEFAULT_DM_ADD_NODE);

//	dm_task_run(dmt);

        if(result < 0)
	{
                printk("\n Error in registering target \n");
		return result;
	}


	
	

	alg = crypto_alloc_shash("crc32c", 0,0);
	if (IS_ERR(alg))
	{
		pr_info("can't alloc alg crc32c\n");
		return PTR_ERR(alg);
	}


	
        return 0;

}




static void cleanup_basic_target(void)

{

	
        dm_unregister_target(&basic_target);

	crypto_free_shash(alg);

}

module_init(init_basic_target);

module_exit(cleanup_basic_target);

MODULE_LICENSE("GPL");

