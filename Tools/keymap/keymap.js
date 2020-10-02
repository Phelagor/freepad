/*
########################################################
######                  keymapper                #######
########################################################

to generate keymap for new function.

Set the pins which are used in row_bits and col_bits.

Run this scrip in a console (like node keymap.js) and take the result for C Keymap

*/
const row_bits = [1,2]
const col_bits = [2,3,4,5]

fs = require('fs');

bin_log = (num) => {
    var n = num.toString(2)
    var orig = "00000000".substr(n.length) + n
    return orig.substring(0, 4) + '.' + orig.substring(4);
}

hex_log = (num) => {
    var n = num.toString(16)
    return "0x"+ "00".substr(n.length) + n
}

convert_bits_to_amount = (bits) => {
    var amount = bits & 1;
    for(bits; bits>0x00; bits>>=1, amount += bits & 1);
    return amount
}

set_bit_mask_bits = (...args) => {
    bit_mask = 0;
    args.forEach(arg => {
        n = 0x01;
        n<<=arg
        bit_mask |= n
    })
    return bit_mask
}

/*
rows to high nibble,
cols to low nibble
*/
convert_to_key = (row, col) => {
    return  (_determine_bits_position(row) << 4 | _determine_bits_position(col))
}
//This function determines log_2() and such the MSB
_determine_bits_position = (input) => {
    //Since we only check unsigned byte, pow 4 is max

    //Checking if in upper nibble (pow 4) and shifting to lower one (max pow 3)
    var result = (input > 0xF) << 2; input >>= result;
    //If it's below 3 (0011) we only have two options left (10 or 01) and can add them directly (|)
    var shift = (input > 0x3 ) << 1;
    return result|shift|(input >> (shift+1));
}

main = async () => {
    row_start_bit = 0x01 << row_bits[0]
    col_start_bit = 0x01 << col_bits[0]

    row_limit = row_start_bit << row_bits.length-1;
    col_limit = col_start_bit << col_bits.length-1;

    line_counter = 0;

    for (row = row_start_bit; row <= row_limit; row <<= 1) {
        for (col = col_start_bit; col <= col_limit; col <<= 1) {
            console.log(`\t/* ######################## Button ${++line_counter} ########################`)
            console.log(`\t\trow: ${bin_log(row)}`)
            console.log(`\t\tcol: ${bin_log(col)}`)
            key = convert_to_key(row,col);
            console.log(`\t\tkey: ${bin_log(key)}`)
            console.log('\t########################################################### */')
            console.log(`\tcase ${hex_log(key)}:`)
            console.log(`\t\treturn ${line_counter};\n`)
        }
    }
}

main();