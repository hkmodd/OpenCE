#ifndef STRUCTURE_BSP_IMAGES_H
#define STRUCTURE_BSP_IMAGES_H

/* a map's structure bsps kept as they were read and checked
(structure_bsp_images.c) */

boolean structure_bsp_images_kept(void);
void structure_bsp_images_keep_begun(void);
boolean structure_bsp_image_restore(long tag_index, long file_offset, long size, void *address);
void structure_bsp_image_keep(long tag_index, long file_offset, long size, void const *address);
void *structure_bsp_image_attachment(void);
void *structure_bsp_image_attach(long size, void (*dispose)(void *data));
void structure_bsp_images_dispose(void);

#endif
