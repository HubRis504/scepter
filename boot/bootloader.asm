; ===========================================================================
; Scepter Bootloader - Stage 1 (MBR)
; 
; Loads at 0x7C00, reads kernel header, enters protected mode, loads kernel
; ===========================================================================

[BITS 16]
[ORG 0x7C00]

KERNEL_LOAD_ADDR equ 0x100000   ; Physical address to load kernel

; ---------------------------------------------------------------------------
; Boot sector entry point
; ---------------------------------------------------------------------------
start:
    cli
    
    ; Setup segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    
    sti
    
    ; Save boot drive
    mov [boot_drive], dl
    
    ; Print boot message
    mov si, msg_boot
    call print_string
    
    ; Enable A20 line
    call enable_a20
    
    ; Read first sector of kernel (contains header) to 0x7E00
    mov si, msg_reading_header
    call print_string
    
    mov ah, 0x02              ; BIOS read sectors
    mov al, 1                 ; 1 sector
    mov ch, 0                 ; Cylinder 0
    mov cl, 2                 ; Sector 2 (sector 1 is boot, sector 2+ is kernel)
    mov dh, 0                 ; Head 0
    mov dl, [boot_drive]
    mov bx, 0x7E00            ; Temp buffer
    int 0x13
    jc disk_error
    
    ; Verify magic number 0x534B524E ("SKRN")
    mov eax, [0x7E00]
    cmp eax, 0x534B524E
    jne magic_error
    
    ; Get kernel size from header (offset +4)
    mov eax, [0x7E04]
    mov [kernel_size], eax
    
    ; Get entry point from header (offset +8)
    mov eax, [0x7E08]
    mov [kernel_entry], eax
    
    ; Calculate sectors = (size + 511) / 512
    mov eax, [kernel_size]
    add eax, 511
    shr eax, 9
    mov [kernel_sectors], eax
    
    mov si, msg_kernel_ok
    call print_string
    
    ; Setup GDT and enter protected mode
    mov si, msg_entering_pm
    call print_string
    
    cli
    lgdt [gdt_descriptor]
    
    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump to 32-bit code
    jmp 0x08:protected_mode

; ---------------------------------------------------------------------------
; Enable A20 line
; ---------------------------------------------------------------------------
enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; ---------------------------------------------------------------------------
; Print string (SI = pointer)
; ---------------------------------------------------------------------------
print_string:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .loop
.done:
    popa
    ret

; ---------------------------------------------------------------------------
; Error handlers
; ---------------------------------------------------------------------------
disk_error:
    mov si, msg_disk_error
    call print_string
    jmp halt

magic_error:
    mov si, msg_magic_error
    call print_string
    jmp halt

halt:
    cli
    hlt
    jmp halt

; ===========================================================================
; Protected Mode (32-bit)
; ===========================================================================

[BITS 32]
protected_mode:
    ; Setup segments
    mov ax, 0x10
    mov ds, eax
    mov es, eax
    mov fs, eax
    mov gs, eax
    mov ss, eax
    mov esp, 0x90000
    
    ; Load kernel to 0x100000 using ATA PIO
    call load_kernel_ata

    
    ; Jump to kernel entry (skip 16-byte header)
    ; Physical address 0x100000 + 0x10 = 0x100010
    jmp KERNEL_LOAD_ADDR + 0x10

; ---------------------------------------------------------------------------
; Load kernel using ATA PIO
; ---------------------------------------------------------------------------
load_kernel_ata:
    mov edi, KERNEL_LOAD_ADDR
    mov eax, 1                ; Start at LBA 1 (sector 0 is boot)
    mov ecx, [kernel_sectors]
    
.read_loop:
    push eax
    push ecx
    
    call ata_read_sector
    
    pop ecx
    pop eax
    
    add edi, 512
    inc eax
    loop .read_loop
    
    ret

; ---------------------------------------------------------------------------
; Read one sector via ATA PIO
; Input: EAX = LBA, EDI = destination
; ---------------------------------------------------------------------------
ata_read_sector:
    pushad
    
    ; Save LBA and destination
    mov ebx, eax              ; EBX = LBA
    mov esi, edi              ; ESI = destination
    
    ; Wait for drive ready
    call ata_wait_ready
    
    ; Send sector count (1)
    mov dx, 0x1F2
    mov al, 1
    out dx, al
    
    ; Send LBA
    mov eax, ebx
    mov dx, 0x1F3
    out dx, al                ; LBA bits 0-7
    
    shr eax, 8
    mov dx, 0x1F4
    out dx, al                ; LBA bits 8-15
    
    shr eax, 8
    mov dx, 0x1F5
    out dx, al                ; LBA bits 16-23
    
    shr eax, 8
    and al, 0x0F
    or al, 0xE0               ; LBA mode, master drive
    mov dx, 0x1F6
    out dx, al                ; LBA bits 24-27 + mode
    
    ; Send read command
    mov dx, 0x1F7
    mov al, 0x20
    out dx, al
    
    ; Wait for data ready
    call ata_wait_data
    
    ; Read 256 words (512 bytes) to [ESI]
    mov edi, esi
    mov ecx, 256
    mov dx, 0x1F0
    rep insw
    
    popad
    ret

ata_wait_ready:
    push eax
    push edx
    mov dx, 0x1F7
.wait:
    in al, dx
    and al, 0xC0
    cmp al, 0x40
    jne .wait
    pop edx
    pop eax
    ret

ata_wait_data:
    push eax
    push edx
    mov dx, 0x1F7
.wait:
    in al, dx
    test al, 0x08
    jz .wait
    pop edx
    pop eax
    ret

; ===========================================================================
; GDT
; ===========================================================================

align 8
gdt_start:
    dq 0x0000000000000000     ; Null
    dq 0x00CF9A000000FFFF     ; Code: base 0, limit 4GB, 32-bit
    dq 0x00CF92000000FFFF     ; Data: base 0, limit 4GB, 32-bit
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0x7C00 + (gdt_start - start)

; ===========================================================================
; Data
; ===========================================================================

boot_drive:     db 0
kernel_size:    dd 0
kernel_sectors: dd 0
kernel_entry:   dd 0

msg_boot:           db "Scepter Boot", 13, 10, 0
msg_reading_header: db "Reading...", 13, 10, 0
msg_kernel_ok:      db "OK", 13, 10, 0
msg_entering_pm:    db "PM...", 13, 10, 0
msg_disk_error:     db "Disk ERR!", 13, 10, 0
msg_magic_error:    db "Bad magic!", 13, 10, 0

; ===========================================================================
; Boot signature
; ===========================================================================

times 510-($-$$) db 0
dw 0xAA55