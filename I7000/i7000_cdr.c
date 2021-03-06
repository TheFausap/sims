/* i7000_cdr.c: IBM 7000 Card reader.

   Copyright (c) 2005-2016, Richard Cornwell

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is the standard card reader.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i7000_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#ifdef NUM_DEVS_CDR

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | \
                         UNIT_ROABLE | MODE_026

/* Flags for punch and reader. */
#define ATTENA          (1 << (UNIT_V_UF+7))
#define ATTENB          (1 << (UNIT_V_UF+14))


/* std devices. data structures

   cdr_dev      Card Reader device descriptor
   cdr_unit     Card Reader unit descriptor
   cdr_reg      Card Reader register list
   cdr_mod      Card Reader modifiers list
*/

/* Device status information stored in u5 */
#define URCSTA_EOF      0001    /* Hit end of file */
#define URCSTA_ERR      0002    /* Error reading record */
#define URCSTA_CARD     0004    /* Unit has card in buffer */
#define URCSTA_FULL     0004    /* Unit has full buffer */
#define URCSTA_BUSY     0010    /* Device is busy */
#define URCSTA_WDISCO   0020    /* Device is wait for disconnect */
#define URCSTA_READ     0040    /* Device is reading channel */
#define URCSTA_WRITE    0100    /* Device is reading channel */
#define URCSTA_INPUT    0200    /* Console fill buffer from keyboard */
#define URCSTA_WMKS     0400    /* Printer print WM as 1 */
#define URCSTA_SKIPAFT  01000   /* Skip to line after printing next line */
#define URCSTA_NOXFER   01000   /* Don't set up to transfer after feed */
#define URCSTA_LOAD     01000   /* Load flag for 7070 card reader */

uint32              cdr_cmd(UNIT *, uint16, uint16);
t_stat              cdr_boot(int32, DEVICE *);
t_stat              cdr_srv(UNIT *);
t_stat              cdr_reset(DEVICE *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);
extern t_stat       chan_boot(int32, DEVICE *);
#ifdef I7070
t_stat              cdr_setload(UNIT *, int32, CONST char *, void *);
t_stat              cdr_getload(FILE *, UNIT *, int32, CONST void *);
#endif
t_stat              cdr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdr_description(DEVICE *dptr);

UNIT                cdr_unit[] = {
   {UDATA(cdr_srv, UNIT_S_CHAN(CHAN_CHUREC) | UNIT_CDR, 0), 300},       /* A */
#if NUM_DEVS_CDR > 1
   {UDATA(cdr_srv, UNIT_S_CHAN(CHAN_CHUREC+1) | UNIT_CDR, 0), 300},     /* B */
#endif
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},    
#ifdef I7070
    {ATTENA|ATTENB, 0, NULL, "NOATTEN", NULL, NULL, NULL},
    {ATTENA|ATTENB, ATTENA, "ATTENA", "ATTENA", NULL, NULL, NULL},
    {ATTENA|ATTENB, ATTENB, "ATTENB", "ATTENB", NULL, NULL, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "LCOL", "LCOL", &cdr_setload,
        &cdr_getload, NULL},
#endif
#ifdef I7010
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
        &get_chan, NULL},
#endif   
    {0}
};

DEVICE              cdr_dev = {
    "CR", cdr_unit, NULL, cdr_mod,
    NUM_DEVS_CDR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, &cdr_boot, &cdr_attach, &sim_card_detach,
    &cdr_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdr_help, NULL, NULL, &cdr_description
};


/*
 * Device entry points for card reader.
 */
uint32 cdr_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdr_unit);
    int                 stk = dev & 017;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_READ)
        return SCPE_BUSY;

    /* Test ready */
    if (cmd == IO_TRS && uptr->flags & UNIT_ATT) {
        sim_debug(DEBUG_CMD, &cdr_dev, "%d: Test Rdy\n", u);
        return SCPE_OK;
    }

    if (stk == 10)
        stk = 0;

#ifdef STACK_DEV
    uptr->u5 &= ~0xF0000;
    uptr->u5 |= stk << 16;
#endif
    /* Process commands */
    switch(cmd) {
    case IO_RDS:
        sim_debug(DEBUG_CMD, &cdr_dev, "%d: Cmd RDS %02o\n", u, dev & 077);
#ifdef I7010
        if (stk!= 9) 
#endif
        uptr->u5 &= ~(URCSTA_CARD|URCSTA_ERR);
        break;
    case IO_CTL:
        sim_debug(DEBUG_CMD, &cdr_dev, "%d: Cmd CTL %02o\n", u, dev & 077);
#ifdef I7010
        uptr->u5 |= URCSTA_NOXFER;
#endif
        break;
    default:
        chan_set_attn(chan);
        return SCPE_IOERR;
    }

    /* If at eof, just return EOF */
    if (uptr->u5 & URCSTA_EOF) {
        chan_set_eof(chan);
        chan_set_attn(chan);
        return SCPE_OK;
    }

    uptr->u5 |= URCSTA_READ;
    uptr->u4 = 0;

    if ((uptr->u5 & URCSTA_NOXFER) == 0)
        chan_set_sel(chan, 0);
    /* Wake it up if not busy */
    if ((uptr->u5 & URCSTA_BUSY) == 0) 
        sim_activate(uptr, 50);
    return SCPE_OK;
}

/* Handle transfer of data for card reader */
t_stat
cdr_srv(UNIT *uptr) {
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdr_unit);
    struct _card_data   *data;

    data = (struct _card_data *)uptr->up7;

    /* Waiting for disconnect */
    if (uptr->u5 & URCSTA_WDISCO) {
        if (chan_stat(chan, DEV_DISCO)) {
            chan_clear(chan, DEV_SEL|DEV_WEOR);
            uptr->u5 &= ~ URCSTA_WDISCO;
        } else {
            /* No disco yet, try again in a bit */
            sim_activate(uptr, 50);
            return SCPE_OK;
        }
        /* If still busy, schedule another wait */
        if (uptr->u5 & URCSTA_BUSY)
             sim_activate(uptr, uptr->wait);
    }

    if (uptr->u5 & URCSTA_BUSY) {
        uptr->u5 &= ~URCSTA_BUSY;
#ifdef I7070
        switch(uptr->flags & (ATTENA|ATTENB)) {
        case ATTENA: chan_set_attn_a(chan); break;
        case ATTENB: chan_set_attn_b(chan); break;
        }
#endif
    }

    /* Check if new card requested. */
    if (uptr->u4 == 0 && uptr->u5 & URCSTA_READ &&
                (uptr->u5 & URCSTA_CARD) == 0) {
        switch(sim_read_card(uptr)) {
        case SCPE_EOF:
        case SCPE_UNATT:
             chan_set_eof(chan);
             chan_set_attn(chan);
             chan_clear(chan, DEV_SEL);
             uptr->u5 &= ~URCSTA_BUSY;
             return SCPE_OK;
        case SCPE_IOERR:
             uptr->u5 |= URCSTA_ERR;
             uptr->u5 &= ~URCSTA_BUSY;
             chan_set_attn(chan);
             chan_clear(chan, DEV_SEL);
             return SCPE_OK;
        case SCPE_OK:   
             uptr->u5 |= URCSTA_CARD;
#ifdef I7010
             chan_set_attn_urec(chan, cdr_dib.addr);
#endif
             break;
        }
#ifdef I7070
        /* Check if load card. */
        if (uptr->capac && (data->image[uptr->capac] & 0x800)) {
             uptr->u5 |= URCSTA_LOAD;
        } else {
             uptr->u5 &= ~URCSTA_LOAD;
        }
#endif
    }

    if (uptr->u5 & URCSTA_NOXFER) {
        uptr->u5 &= ~(URCSTA_NOXFER|URCSTA_READ);
        return SCPE_OK;
    }

    /* Copy next column over */
    if (uptr->u5 & URCSTA_READ && uptr->u4 < 80) {
        uint8                ch = 0;

#ifdef I7080
        /* Detect RSU */
        if (data->image[uptr->u4] == 0x924) {
             uptr->u5 &= ~URCSTA_READ;
             uptr->u5 |= URCSTA_WDISCO;
             chan_set(chan, DEV_REOR);
             sim_activate(uptr, 10);
             return SCPE_OK;
        }
#endif
              
        ch = sim_hol_to_bcd(data->image[uptr->u4]);

        /* Handle invalid punch */
        if (ch == 0x7f) {
#ifdef I7080
             uptr->u5 &= ~(URCSTA_READ|URCSTA_BUSY);
             chan_set_attn(chan);
             chan_clear(chan, DEV_SEL);
#else
             
             uptr->u5 |= URCSTA_ERR;
             ch = 017;
#endif
        }
        switch(chan_write_char(chan, &ch, (uptr->u4 == 79)? DEV_REOR: 0)) {
        case TIME_ERROR:
        case END_RECORD:
            uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY;
            uptr->u5 &= ~URCSTA_READ;
            break;
        case DATA_OK:
            uptr->u4++;
            break;
        }
        sim_debug(DEBUG_DATA, &cdr_dev, "%d: Char > %02o\n", u, ch);
        sim_activate(uptr, 10);
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
cdr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_stat              r;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
    /* Read in one record */
    r = cdr_cmd(uptr, IO_RDS, cdr_dib.addr);
    if (r != SCPE_OK)
        return r;
    r = chan_boot(unit_num, dptr);
    return r;
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 &= URCSTA_BUSY|URCSTA_WDISCO;
    uptr->u4 = 0;
    uptr->u6 = 0;
#ifdef I7010
    chan_set_attn_urec(UNIT_G_CHAN(uptr->flags), cdr_dib.addr);
#endif
    return SCPE_OK;
}
#ifdef I7070
t_stat
cdr_setload(UNIT *uptr, int32 val, CONST char *cptr, void *desc) 
{
    int i;
    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    i = 0;
    while(*cptr != '\0') {
        if (*cptr < '0' || *cptr > '9')
           return SCPE_ARG;
        i = (i * 10) + (*cptr++) - '0';
    }
    if (i > 80)
        return SCPE_ARG;
    uptr->capac = i;
    return SCPE_OK;
}

t_stat
cdr_getload(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "loadcolumn=%d", uptr->capac);
    return SCPE_OK;
}
#endif

t_stat
cdr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Card Reader\n\n");
   fprintf (st, "The system supports up to two card readers.\n");
#ifdef I7070
   fprintf (st, " Atten and LCOL\n");
#endif
#ifdef I7010
   fprintf (st, " channel\n");
#endif   
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdr_description(DEVICE *dptr)
{
   return "Card Reader";
}

#endif

