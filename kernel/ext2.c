#include <kernel/ext2.h>
#include <kernel/alloc.h>
#include <kernel/virtio-blk.h>
#include <kernel/kprintf.h>
#include <kernel/klib.h>
#include <kernel/errno.h>

ext2_blkdev_t ext2_dev_list;

static int ext2_init_superblock_read(ext2_superblock_t *superblock,
		size_t virtio_devnum)
{
	int err = 0;
	err = virtio_blk_read_nosleep(virtio_devnum,
			EXT2_SUPERBLOCK_START / VIRTIO_BLK_SECTOR_SIZE,
			superblock);
	return err;
}

void ext2_init(void)
{
	extern virtio_blk_t virtio_blk_list[VIRTIO_MAX];
	int err;
	ext2_superblock_t superblock;
	
	list_init(&ext2_dev_list.devlist);

	for (size_t i = 0; i < VIRTIO_MAX; i++)	{
		if (virtio_blk_list[i].isvalid) {
			ext2_blkdev_t *blkdev;
			err = ext2_init_superblock_read(&superblock, i);
			if (err) {
				continue;
			}

			/* check ext2 magic value */
			if (superblock.s_magic != EXT2_SUPER_MAGIC) {
				continue;
			}

			blkdev = kmalloc(sizeof(*blkdev));

			blkdev->virtio_devnum = i;
			blkdev->block_size = 1024 << superblock.s_log_block_size;
			blkdev->inodes_count = superblock.s_inodes_count;
			blkdev->blocks_count = superblock.s_blocks_count;
			blkdev->inodes_per_group = superblock.s_inodes_per_group;
			blkdev->blocks_per_group = superblock.s_blocks_per_group;

			blkdev->blockgroups_count =
				blkdev->inodes_count % blkdev->inodes_per_group ?
				blkdev->inodes_count / blkdev->inodes_per_group + 1 :
				blkdev->inodes_count / blkdev->inodes_per_group;

			blkdev->rev_level = superblock.s_rev_level;

			if (superblock.s_rev_level == EXT2_GOOD_OLD_REV) {
				blkdev->inode_size = EXT2_INODE_SIZE;
			} else {
				blkdev->inode_size = superblock.s_inode_size;
			}

			spinlock_init(&blkdev->lock);

			list_add(&blkdev->devlist, &ext2_dev_list.devlist);
		}
	}
}

int ext2_block_read(ext2_blkdev_t *dev, u64 blknum, void *buf)
{
	int err;
	for (size_t i = 0; i < dev->block_size / VIRTIO_BLK_SECTOR_SIZE; i++) {
		err = virtio_blk_read(dev->virtio_devnum,
				blknum * dev->block_size / VIRTIO_BLK_SECTOR_SIZE + i,
				buf + i * VIRTIO_BLK_SECTOR_SIZE);
		if (err) {
			return err;
		}
	}
	return 0;
}

int ext2_block_write(ext2_blkdev_t *dev, u64 blknum, void *buf)
{
	int err;
	for (size_t i = 0; i < dev->block_size / VIRTIO_BLK_SECTOR_SIZE; i++) {
		err = virtio_blk_write(dev->virtio_devnum,
				blknum * dev->block_size / VIRTIO_BLK_SECTOR_SIZE + i,
				buf + i * VIRTIO_BLK_SECTOR_SIZE);
		if (err) {
			return err;
		}
	}
	return 0;
}

int ext2_nbytes_read(ext2_blkdev_t *dev, void *buf, u64 len, u64 offset)
{
	int err;
	u8 blockbuf[dev->block_size];
	u64 firstblock = offset / dev->block_size;
	u64 lastblock = (offset + len) / dev->block_size;
	u64 ncopied = 0;
	u64 inblock_off, inblock_len;

	for (u64 curblock = firstblock; curblock <= lastblock; curblock++) {
		if (curblock == firstblock) {
			inblock_off = offset - curblock * dev->block_size;
			if (dev->block_size - inblock_off > len) {
				inblock_len = len;
			} else {
				inblock_len = dev->block_size - inblock_off;
			}
		} else if (curblock == lastblock) {
			inblock_off = 0;
			inblock_len = len - ncopied;
		} else {
			inblock_off = 0;
			inblock_len = dev->block_size;
		}

		err = ext2_block_read(dev, curblock, blockbuf);
		if (err) {
			return err;
		}

		memcpy((u8 *) buf + ncopied, blockbuf + inblock_off, inblock_len);

		ncopied += inblock_len;
	}

	return 0;
}

int ext2_nbytes_write(ext2_blkdev_t *dev, void *buf, u64 len, u64 offset)
{
	int err;
	u8 blockbuf[dev->block_size];
	u64 firstblock = offset / dev->block_size;
	u64 lastblock = (offset + len) / dev->block_size;
	u64 ncopied = 0;
	u64 inblock_off, inblock_len;

	for (u64 curblock = firstblock; curblock <= lastblock; curblock++) {
		if (curblock == firstblock) {
			inblock_off = offset - curblock * dev->block_size;
			if (dev->block_size - inblock_off > len) {
				inblock_len = len;
			} else {
				inblock_len = dev->block_size - inblock_off;
			}

			err = ext2_block_read(dev, curblock, blockbuf);
			if (err) {
				return err;
			}
		} else if (curblock == lastblock) {
			inblock_off = 0;
			inblock_len = len - ncopied;
		} else {
			inblock_off = 0;
			inblock_len = dev->block_size;

			err = ext2_block_read(dev, curblock, blockbuf);
			if (err) {
				return err;
			}
		}

		memcpy(blockbuf + inblock_off, (u8 *) buf + ncopied, inblock_len);

		err = ext2_block_write(dev, curblock, blockbuf);
		if (err) {
			return err;
		}

		ncopied += inblock_len;
	}

	return 0;
}

int ext2_superblock_read(ext2_blkdev_t *dev, ext2_superblock_t *superblock)
{
	int err;

	err = ext2_nbytes_read(dev, superblock, sizeof(*superblock),
			EXT2_SUPERBLOCK_START);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_superblock_write_all_copies(ext2_blkdev_t *dev, ext2_superblock_t *superblock)
{
	int err;

	/* update 0 copy */
	err = ext2_nbytes_write(dev, superblock, sizeof(*superblock),
			EXT2_SUPERBLOCK_START);
	if (err) {
		return err;
	}

	/* update 1 copy */
	if (dev->blockgroups_count > 0) {
		return 0;
	}
	err = ext2_nbytes_write(dev, superblock, sizeof(*superblock),
			EXT2_SUPERBLOCK_START +
			dev->blocks_per_group);
	if (err) {
		return err;
	}

	/* power of 3 */
	for (size_t i = 3; i < dev->blockgroups_count; i *= 3) {
		err = ext2_nbytes_write(dev, superblock, sizeof(*superblock),
			EXT2_SUPERBLOCK_START +
			dev->blocks_per_group * i);
		if (err) {
			return err;
		}
	}

	/* power of 5 */
	for (size_t i = 5; i < dev->blockgroups_count; i *= 5) {
		err = ext2_nbytes_write(dev, superblock, sizeof(*superblock),
			EXT2_SUPERBLOCK_START +
			dev->blocks_per_group * i);
		if (err) {
			return err;
		}
	}

	/* power of 7 */
	for (size_t i = 7; i < dev->blockgroups_count; i *= 7) {
		err = ext2_nbytes_write(dev, superblock, sizeof(*superblock),
			EXT2_SUPERBLOCK_START +
			dev->blocks_per_group * i);
		if (err) {
			return err;
		}
	}

	return 0;
}

int ext2_blockgroup_descriptor_read(ext2_blkdev_t *dev, u32 bgnum,
		ext2_blockgroup_descriptor_t *bgdesc)
{
	int err;

	err = ext2_nbytes_read(dev, bgdesc, sizeof(*bgdesc),
			EXT2_BLOCKGROUP_DESCRIPTOR_TABLE_START +
			bgnum * sizeof(*bgdesc));
	if (err) {
		return err;
	}

	return 0;
}

int ext2_blockgroup_descriptor_write_all_copies(ext2_blkdev_t *dev, u32 bgnum,
		ext2_blockgroup_descriptor_t *bgdesc)
{
	int err;

	/* update 0 copy */
	err = ext2_nbytes_write(dev, bgdesc, sizeof(*bgdesc),
			EXT2_BLOCKGROUP_DESCRIPTOR_TABLE_START +
			bgnum * sizeof(*bgdesc));
	if (err) {
		return err;
	}

	/* update 1 copy */
	if (dev->blockgroups_count > 0) {
		return 0;
	}
	err = ext2_nbytes_write(dev, bgdesc, sizeof(*bgdesc),
			EXT2_BLOCKGROUP_DESCRIPTOR_TABLE_START +
			bgnum * sizeof(*bgdesc) +
			dev->blocks_per_group);
	if (err) {
		return err;
	}

	/* power of 3 */
	for (size_t i = 3; i < dev->blockgroups_count; i *= 3) {
		err = ext2_nbytes_write(dev, bgdesc, sizeof(*bgdesc),
			EXT2_BLOCKGROUP_DESCRIPTOR_TABLE_START +
			bgnum * sizeof(*bgdesc) +
			dev->blocks_per_group * i);
		if (err) {
			return err;
		}
	}

	/* power of 5 */
	for (size_t i = 5; i < dev->blockgroups_count; i *= 5) {
		err = ext2_nbytes_write(dev, bgdesc, sizeof(*bgdesc),
			EXT2_BLOCKGROUP_DESCRIPTOR_TABLE_START +
			bgnum * sizeof(*bgdesc) +
			dev->blocks_per_group * i);
		if (err) {
			return err;
		}
	}

	/* power of 7 */
	for (size_t i = 7; i < dev->blockgroups_count; i *= 7) {
		err = ext2_nbytes_write(dev, bgdesc, sizeof(*bgdesc),
			EXT2_BLOCKGROUP_DESCRIPTOR_TABLE_START +
			bgnum * sizeof(*bgdesc) +
			dev->blocks_per_group * i);
		if (err) {
			return err;
		}
	}

	return 0;
}

int ext2_inode_read(ext2_blkdev_t *dev, u32 inum, ext2_inode_t *inode)
{
	int err;
	u32 bgnum = (inum - 1) / dev->inodes_per_group;
	u32 index = (inum - 1) % dev->inodes_per_group;
	ext2_blockgroup_descriptor_t bgdesc;

	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}

	err = ext2_nbytes_read(dev, inode, sizeof(*inode),
			bgdesc.bg_inode_table * dev->block_size +
			index * dev->inode_size);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_inode_write(ext2_blkdev_t *dev, u32 inum, ext2_inode_t *inode)
{
	int err;
	u32 bgnum = (inum - 1) / dev->inodes_per_group;
	u32 index = (inum - 1) % dev->inodes_per_group;
	ext2_blockgroup_descriptor_t bgdesc;

	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}
	
	err = ext2_nbytes_write(dev, inode, sizeof(*inode),
			bgdesc.bg_inode_table * dev->block_size +
			index * dev->inode_size);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_inode_counter_increment(ext2_blkdev_t *dev, u32 inum)
{
	int err;
	u32 bgnum = (inum - 1) / dev->inodes_per_group;
	ext2_blockgroup_descriptor_t bgdesc;
	ext2_superblock_t superblock;

	/* update all blockgroup copies */
	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}
	bgdesc.bg_free_inodes_count++;
	err = ext2_blockgroup_descriptor_write_all_copies(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}

	/* update all superblock copies */
	err = ext2_superblock_read(dev, &superblock);
	if (err) {
		return err;
	}
	superblock.s_free_inodes_count++;
	err = ext2_superblock_write_all_copies(dev, &superblock);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_inode_counter_decrement(ext2_blkdev_t *dev, u32 inum)
{
	int err;
	u32 bgnum = (inum - 1) / dev->inodes_per_group;
	ext2_blockgroup_descriptor_t bgdesc;
	ext2_superblock_t superblock;

	/* update all blockgroup copies */
	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}
	bgdesc.bg_free_inodes_count--;
	err = ext2_blockgroup_descriptor_write_all_copies(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}

	/* update all superblock copies */
	err = ext2_superblock_read(dev, &superblock);
	if (err) {
		return err;
	}
	superblock.s_free_inodes_count--;
	err = ext2_superblock_write_all_copies(dev, &superblock);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_inode_allocate(ext2_blkdev_t *dev, u32 *inum)
{
	int err;
	ext2_blockgroup_descriptor_t bgdesc;
	char inode_bitmap[dev->block_size];

	for (size_t bgnum = 0; bgnum < dev->blockgroups_count; bgnum++) {
		err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
		if (err) {
			return err;
		}

		err = ext2_block_read(dev, bgdesc.bg_inode_bitmap, inode_bitmap);
		if (err) {
			return err;
		}

		for (size_t i = 0; i < dev->inodes_per_group; i++) {
			if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
				inode_bitmap[i / 8] |= 1 << (i % 8);

				err = ext2_block_write(dev, bgdesc.bg_inode_bitmap,
						inode_bitmap);
				if (err) {
					return err;
				}

				*inum = bgnum * dev->inodes_per_group + i + 1;

				err = ext2_inode_counter_decrement(dev, *inum);
				if (err) {
					return err;
				}

				return 0;
			}
		}
	}

	return -ENOSPC;
}

int ext2_inode_free(ext2_blkdev_t *dev, u32 inum)
{
	int err;
	u32 bgnum = (inum - 1) / dev->inodes_per_group;
	u32 index = (inum - 1) % dev->inodes_per_group;
	ext2_blockgroup_descriptor_t bgdesc;
	char inode_bitmap[dev->block_size];

	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}

	err = ext2_block_read(dev, bgdesc.bg_inode_bitmap, inode_bitmap);
	if (err) {
		return err;
	}

	inode_bitmap[index / 8] &= ~(1 << (index % 8));

	err = ext2_block_write(dev, bgdesc.bg_inode_bitmap,
			inode_bitmap);
	if (err) {
		return err;
	}

	err = ext2_inode_counter_increment(dev, inum);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_block_counter_increment(ext2_blkdev_t *dev, u32 blknum)
{
	int err;
	u32 bgnum = blknum / dev->blocks_per_group;
	ext2_blockgroup_descriptor_t bgdesc;
	ext2_superblock_t superblock;

	/* update all blockgroup copies */
	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}
	bgdesc.bg_free_blocks_count++;
	err = ext2_blockgroup_descriptor_write_all_copies(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}

	/* update all superblock copies */
	err = ext2_superblock_read(dev, &superblock);
	if (err) {
		return err;
	}
	superblock.s_free_blocks_count++;
	err = ext2_superblock_write_all_copies(dev, &superblock);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_block_counter_decrement(ext2_blkdev_t *dev, u32 blknum)
{
	int err;
	u32 bgnum = blknum / dev->blocks_per_group;
	ext2_blockgroup_descriptor_t bgdesc;
	ext2_superblock_t superblock;

	/* update all blockgroup copies */
	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}
	bgdesc.bg_free_blocks_count--;
	err = ext2_blockgroup_descriptor_write_all_copies(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}

	/* update all superblock copies */
	err = ext2_superblock_read(dev, &superblock);
	if (err) {
		return err;
	}
	superblock.s_free_blocks_count--;
	err = ext2_superblock_write_all_copies(dev, &superblock);
	if (err) {
		return err;
	}

	return 0;
}

int ext2_block_allocate(ext2_blkdev_t *dev, u32 *blknum, u32 inode_hint)
{
	int err;
	ext2_blockgroup_descriptor_t bgdesc;
	char block_bitmap[dev->block_size];
	size_t bgnum_hint = (inode_hint - 1) / dev->inodes_per_group;

	/* First we should try to allocate block in the same 
	 * blockgroup in which inode is located.
	 */
	err = ext2_blockgroup_descriptor_read(dev, bgnum_hint, &bgdesc);
	if (err) {
		return err;
	}
	err = ext2_block_read(dev, bgdesc.bg_block_bitmap, block_bitmap);
	if (err) {
		return err;
	}
	for (size_t i = 0; i < dev->blocks_per_group; i++) {
		if (!(block_bitmap[i / 8] & (1 << (i % 8)))) {
			block_bitmap[i / 8] |= 1 << (i % 8);

			err = ext2_block_write(dev, bgdesc.bg_block_bitmap,
					block_bitmap);
			if (err) {
				return err;
			}

			*blknum = bgnum_hint * dev->blocks_per_group + i;

			err = ext2_block_counter_decrement(dev, *blknum);
			if (err) {
				return err;
			}

			return 0;
		}
	}

	/* If inode's blockgroup does not contain free blocks 
	 * try to search for free block in all blockgroups.
	 */
	for (size_t bgnum = 0; bgnum < dev->blockgroups_count; bgnum++) {
		if (bgnum == bgnum_hint) {
			continue;
		}

		err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
		if (err) {
			return err;
		}

		err = ext2_block_read(dev, bgdesc.bg_block_bitmap, block_bitmap);
		if (err) {
			return err;
		}

		for (size_t i = 0; i < dev->blocks_per_group; i++) {
			if (!(block_bitmap[i / 8] & (1 << (i % 8)))) {
				block_bitmap[i / 8] |= 1 << (i % 8);

				err = ext2_block_write(dev, bgdesc.bg_block_bitmap,
						block_bitmap);
				if (err) {
					return err;
				}

				*blknum = bgnum * dev->blocks_per_group + i;
				
				err = ext2_block_counter_decrement(dev, *blknum);
				if (err) {
					return err;
				}

				return 0;
			}
		}
	}

	return -ENOSPC;
}

int ext2_block_free(ext2_blkdev_t *dev, u32 blknum)
{
	int err;
	u32 bgnum = blknum / dev->blocks_per_group;
	u32 index = blknum % dev->blocks_per_group;
	ext2_blockgroup_descriptor_t bgdesc;
	char block_bitmap[dev->block_size];

	err = ext2_blockgroup_descriptor_read(dev, bgnum, &bgdesc);
	if (err) {
		return err;
	}

	err = ext2_block_read(dev, bgdesc.bg_block_bitmap, block_bitmap);
	if (err) {
		return err;
	}

	block_bitmap[index / 8] &= ~(1 << (index % 8));

	err = ext2_block_write(dev, bgdesc.bg_block_bitmap,
			block_bitmap);
	if (err) {
		return err;
	}

	err = ext2_block_counter_increment(dev, blknum);
	if (err) {
		return err;
	}

	return 0;
}

