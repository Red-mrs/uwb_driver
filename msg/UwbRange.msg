# addresses are 32-bit integers, 16-bit integers or 8-bit integers
# based on ranging schema, the unused bits are set to 0
uint32 initiator_address
uint32 responder_address
uint32 own_address

# estimated range from initiator_address to responder_address
float64 distance
