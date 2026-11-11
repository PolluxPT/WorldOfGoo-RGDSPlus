/* Handheld name entry. Shared word 520 is outside the evdev touch ring. */
#define KEYBOARD_WORD 520
static int keyboard_on, keyboard_key;

static void name_key(uint32_t id,int scan,int key)
{
    Event event={0};
    event.key.type=0x300;
    event.key.window=id;
    event.key.state=1;
    event.key.scan=scan;
    event.key.key=key;
    enqueue(event);
    event.key.type=0x301;
    event.key.state=0;
    enqueue(event);
}

static int name_keyboard(uint32_t id,uint32_t bits,uint32_t old)
{
    uint32_t pressed=bits&~old;
    if (pressed&(1u<<19)) {
        keyboard_on=!keyboard_on;
        if (keyboard_on) {
            keyboard_key=0;
            pointer_button(id,0);
        }
        state[KEYBOARD_WORD]=keyboard_on ? keyboard_key+1 : 0;
        return 1;
    }
    if (!keyboard_on) return 0;
    if (pressed&(1u<<20)) keyboard_key=(keyboard_key+27)%28;
    if (pressed&(1u<<21)) keyboard_key=(keyboard_key+1)%28;
    if (pressed&(1u<<22)) keyboard_key=(keyboard_key+21)%28;
    if (pressed&(1u<<23)) keyboard_key=(keyboard_key+7)%28;
    if (pressed&(1u<<16)) {
        if (keyboard_key==27) name_key(id,42,8);
        else {
            Event event={0};
            event.type=0x303;
            memcpy(event.b+8,&id,4);
            event.b[12]=keyboard_key==26 ? ' ' : 'A'+keyboard_key;
            enqueue(event);
        }
    }
    if (pressed&(1u<<17)) name_key(id,42,8);
    if (pressed&(1u<<27)) {
        name_key(id,40,13);
        keyboard_on=0;
    }
    state[KEYBOARD_WORD]=keyboard_on ? keyboard_key+1 : 0;
    return 1;
}
