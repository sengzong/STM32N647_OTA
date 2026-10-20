# Agent LCD initialization

Adds LTDC and DMA2D handles and copies the working management application's
peripheral initialization. RGBLCD panel detection, pixel clock and portrait
rotation remain in the existing BSP. GPIO/RIF setup precedes this function.
No camera or touch initialization is added to the receiver.

The LCD framebuffer is internal SRAM and is independent of PSRAM reception.
Check the linked SRAM/stack/retained mailbox boundaries and the 1 MiB compatible
Agent partition before publishing a new build. Real panel operation still needs
board verification after flashing the rebuilt Agent.
