/**
 * @file main.c
 * @brief AESD char driver for the AESD assignment
 */

 #include <linux/module.h>
 #include <linux/init.h>
 #include <linux/printk.h>
 #include <linux/types.h>
 #include <linux/cdev.h>
 #include <linux/fs.h>      // file_operations
 #include <linux/slab.h>    // kmalloc, kfree, krealloc
 #include <linux/uaccess.h>
 #include <linux/mutex.h>
 #include "aesdchar.h"   
 #include "aesd_ioctl.h"
 
 int aesd_major = 0; 
 int aesd_minor = 0;
 
 #define DEVICE_NAME "aesdchar"
 MODULE_AUTHOR("Jainil");
 MODULE_LICENSE("Dual BSD/GPL");
 
 struct aesd_dev aesd_device; 
 

 static int aesd_open(struct inode *inode, struct file *filp);
 static int aesd_release(struct inode *inode, struct file *filp);
 static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
 static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
 static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
 static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
 

 static struct file_operations aesd_fops = {
     .owner   = THIS_MODULE,
     .open    = aesd_open,
     .release = aesd_release,
     .read    = aesd_read,
     .write   = aesd_write,
     .llseek  = aesd_llseek,
     .unlocked_ioctl = aesd_ioctl
 };
 

 static int aesd_open(struct inode *inode, struct file *filp)
 {
     struct aesd_dev *dev;
     PDEBUG("open\n");
 
     dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
     filp->private_data = dev;
     filp->f_pos = 0;
     return 0;
 }
 
 static int aesd_release(struct inode *inode, struct file *filp)
 {
     PDEBUG("release\n");
     filp->private_data = NULL;
     return 0;
 }
 
 static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
 {
     struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
     struct aesd_buffer_entry *entry;
     size_t entry_offset;
     ssize_t retval        = 0;
     size_t read_size;
     size_t bytes_not_copied;
 
     PDEBUG("read %zu bytes with offset %lld\n", count, *f_pos);
 
     if (mutex_lock_interruptible(&dev->lock))
     {
        return -ERESTARTSYS;
     }
         
 
     entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circbuf, *f_pos, &entry_offset);
     if (!entry || entry->buffptr == NULL) 
     {
         retval = 0;
         goto out_unlock;
     }
 
     read_size = entry->size - entry_offset;
     if (read_size > count)
     {
         read_size = count;
     }

     bytes_not_copied = copy_to_user(buf, entry->buffptr + entry_offset, read_size);
     
     if (bytes_not_copied) 
     {
         retval = -EFAULT;
         goto out_unlock;
     }
 
     *f_pos += (read_size - bytes_not_copied);
     retval  = (read_size - bytes_not_copied);
 
 out_unlock:
     mutex_unlock(&dev->lock);
     return retval;
 }
 
 static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
 {
     struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
     ssize_t retval = 0;
     size_t bytes_not_copied;
     char *new_buf;
     size_t i = 0;
     char *newline_ptr;
 
     PDEBUG("write %zu bytes with offset %lld\n", count, *f_pos);
 
     if (mutex_lock_interruptible(&dev->lock))
     {
         return -ERESTARTSYS;
     }

     new_buf = krealloc(dev->pending_write_buf, dev->pending_size + count, GFP_KERNEL);
     if (!new_buf) 
     {
         retval = -ENOMEM;
         goto out_unlock;
     }
     dev->pending_write_buf = new_buf;
 
     bytes_not_copied = copy_from_user(&dev->pending_write_buf[dev->pending_size], buf, count);
     if (bytes_not_copied) 
     {
         retval = -EFAULT;
         goto out_unlock;
     }
 
     dev->pending_size += (count - bytes_not_copied);
     retval = (count - bytes_not_copied); 
 
     while (1) 
     {
         newline_ptr = memchr(&dev->pending_write_buf[i], '\n', dev->pending_size - i);
         if (!newline_ptr) 
         {
             break;
         }
 
         size_t command_len = (newline_ptr - &dev->pending_write_buf[i]) + 1;
         struct aesd_buffer_entry entry;
         entry.size    = command_len;
         entry.buffptr = kmalloc(command_len, GFP_KERNEL);
         if (!entry.buffptr) 
         {
             retval = -ENOMEM;
             goto out_unlock;
         }
 
         memcpy((char *)entry.buffptr, &dev->pending_write_buf[i], command_len);
 
         {
            const char *oldptr = aesd_circular_buffer_add_entry(&dev->circbuf, &entry);
            if (oldptr) 
            {
                kfree((void *)oldptr);
            }
         }
 
         i += command_len;
     }
 
     if (i > 0) 
     {
         size_t leftover = dev->pending_size - i;
         if (leftover > 0) 
         {
             memmove(dev->pending_write_buf, &dev->pending_write_buf[i], leftover);
         }
         dev->pending_size = leftover;
 
         if (leftover == 0) 
         {
             kfree(dev->pending_write_buf);
             dev->pending_write_buf = NULL;
         } 
         else 
         {
             char *shrink = krealloc(dev->pending_write_buf, leftover, GFP_KERNEL);
             if (shrink)
             {
                 dev->pending_write_buf = shrink;
             }
        }
     }
 
 out_unlock:
     mutex_unlock(&dev->lock);
     return retval;
 }
 
 static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
 {
    PDEBUG("llseek called: offset = %lld, whence = %d\n", offset, whence);

    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    loff_t newpos;
    int idx = dev->circbuf.out_offs;
    size_t total_size=0;

    switch(whence)
    {
        case SEEK_SET:
            newpos = offset;
            filp->f_pos = newpos;
            return newpos;
        case SEEK_CUR:

            if (mutex_lock_interruptible(&dev->lock))
            {
                return -ERESTARTSYS;
            }
            newpos = filp->f_pos + offset;
            goto out_unlock;

        case SEEK_END:

            if (mutex_lock_interruptible(&dev->lock))
            {
                return -ERESTARTSYS;
            }   
            
            while(idx != dev->circbuf.in_offs)
            {
                total_size += dev->circbuf.entry[idx].size;
                idx=(idx+1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            }

            newpos = total_size + offset;
            goto out_unlock;
        default:
            return -EINVAL;
            break;
    }

out_unlock:
    if(newpos < 0)
    {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }
    
    filp->f_pos = newpos;
    mutex_unlock(&dev->lock);
    return newpos;

 }

static long aesd_adjust_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    int idx = dev->circbuf.out_offs;
    int adjusted_offset = 0;

    if((write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) || (write_cmd_offset >= dev->circbuf.entry[write_cmd].size))
    {
        return -EINVAL;
    }

    while(idx != write_cmd)
    {
        adjusted_offset += dev->circbuf.entry[idx].size;
        idx=(idx+1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    adjusted_offset += write_cmd_offset;

    return adjusted_offset;

}

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    long retval;
    switch(cmd)
    {
        case AESDCHAR_IOCSEEKTO:
        {
            PDEBUG("aesd-ioctl called\n");
            struct aesd_seekto seekto;
        
            if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) 
            {
                retval = -EFAULT;
                goto out;
            }
            else
            {
                if (mutex_lock_interruptible(&dev->lock))
                {
                    return -ERESTARTSYS;
                }  
                retval = aesd_adjust_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
                if(retval < 0)
                {
                    retval = -EINVAL;
                }
                else
                {
                    filp->f_pos = retval;
                }

                goto out_unlock;
            }
            break;
        }
        default:
            retval = -ENOTTY;
            break;
    }


out_unlock:
    mutex_unlock(&dev->lock);
out:
    return retval;

}

 static int aesd_setup_cdev(struct aesd_dev *dev)
 {
     int err;
     int devno = MKDEV(aesd_major, aesd_minor);
 
     cdev_init(&dev->cdev, &aesd_fops);
     dev->cdev.owner = THIS_MODULE;
 
     err = cdev_add(&dev->cdev, devno, 1);
     if (err)
     {
        PDEBUG("Error %d adding aesd cdev\n", err);
     }

     return err;
 }
 
 int aesd_init_module(void)
 {
     dev_t dev = 0;
     int result;
 
     result = alloc_chrdev_region(&dev, aesd_minor, 1, DEVICE_NAME);
     if (result < 0) 
     {
        PDEBUG("Can't get major %d\n", aesd_major);
         return result;
     }
     aesd_major = MAJOR(dev);
 
     memset(&aesd_device, 0, sizeof(struct aesd_dev));
     aesd_circular_buffer_init(&aesd_device.circbuf);
     mutex_init(&aesd_device.lock);
 
     aesd_device.pending_write_buf = NULL;
     aesd_device.pending_size      = 0;
 
     result = aesd_setup_cdev(&aesd_device);
     if (result)
     {
         unregister_chrdev_region(dev, 1);
     }

     PDEBUG("module loaded, major=%d\n", aesd_major);
     return result;
 }
 
 void aesd_cleanup_module(void)
 {
     uint8_t index;
     struct aesd_buffer_entry *entry;
     dev_t devno = MKDEV(aesd_major, aesd_minor);
 
     AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circbuf, index) 
     {
         if (entry->buffptr) 
         {
             kfree((void *)entry->buffptr);
             entry->buffptr = NULL;
         }
     }
 
     if (aesd_device.pending_write_buf) 
     {
         kfree(aesd_device.pending_write_buf);
         aesd_device.pending_write_buf = NULL;
         aesd_device.pending_size = 0;
     }
 
     cdev_del(&aesd_device.cdev);
     unregister_chrdev_region(devno, 1);
 
     PDEBUG("module unloaded\n");
 }
 
 module_init(aesd_init_module);
 module_exit(aesd_cleanup_module);