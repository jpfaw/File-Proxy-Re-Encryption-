// gcc -o pairing pairing.c -ltepla -lssl -lgmp -lcrypto -fopenmp
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#include <gmp.h>
#include <dirent.h>
#include <tepla/ec.h>
#include "settings.h"
#include "openssl/evp.h"

#define ENCRYPT_INFOLDA     "Plain"
#define ENCRYPT_OUTFOLDA    "Enc"
#define RE_ENCRYPT_INFOLDA  "Enc"
#define RE_ENCRYPT_OUTFOLDA "Enc"
#define DECRYPT_INFOLDA     "Enc"
#define DECRYPT_OUTFOLDA    "Dec"

EC_PAIRING p;
EC_POINT P, Q;
mpz_t limit, a, b, r;
char str[1000];
double start_time, finish_time;

void set_crypto_data();
char *get_str_data(char *user, char *data);
void calc_result_str_convert_to_key_origin(char *key, Element calc_result);

// AESを実際に行う関数
int AES(char *in_fname, char *out_fname, unsigned char *key, unsigned char *iv, int do_encrypt){
    // do_encrypt: 1:暗号化 / 0:復号

    // Allow enough space in output buffer for additional block
    // Bogus key and IV: we'd normally set these from another source.

    // unsigned char inbuf[1024], outbuf[1024 + EVP_MAX_BLOCK_LENGTH];
    unsigned char *inbuf, *outbuf;
    int inlen, outlen;

    FILE *fin, *fout;
    fin  = fopen(in_fname, "rb");
    fout = fopen(out_fname, "wb");

    //バッファサイズの設定
    unsigned long in_size;
    in_size = get_file_size(in_fname);
    printf("[size = %9lu]", in_size);

    if((inbuf = malloc(sizeof(char)*in_size)) == NULL)
        error_notice(1000, "inbuf", __func__, __LINE__);
    if((outbuf = malloc(sizeof(char)*(int)(in_size+EVP_MAX_BLOCK_LENGTH))) == NULL)
        error_notice(1000, "outbuf", __func__, __LINE__);
    start_time = omp_get_wtime();
    EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX_init(&ctx);
    EVP_CipherInit_ex(&ctx, EVP_aes_128_cbc(), NULL, NULL, NULL, do_encrypt);
    OPENSSL_assert(EVP_CIPHER_CTX_key_length(&ctx) == 16);
    OPENSSL_assert(EVP_CIPHER_CTX_iv_length(&ctx) == 16);

    //AES128の鍵と初期ベクトルを設定
    EVP_CipherInit_ex(&ctx, NULL, NULL, key, iv, do_encrypt);
    for(;;){
        // ファイルポインタfinからバッファinbufにサイズ1のデータin_size個を読み込む
        // inlenには読み込んだ個数を返却
        inlen = fread(inbuf, 1, in_size, fin);
        if(inlen <= 0) break;
        if(!EVP_CipherUpdate(&ctx, outbuf, &outlen, inbuf, inlen)){ // Error
            EVP_CIPHER_CTX_cleanup(&ctx);
            fcloses(fin, fout, NULL);
            frees(inbuf, outbuf, NULL);
            return 0;
        }
        fwrite(outbuf, 1, outlen, fout);
    }
    if(!EVP_CipherFinal_ex(&ctx, outbuf, &outlen)){ // Error
        EVP_CIPHER_CTX_cleanup(&ctx);
        fcloses(fin, fout, NULL);
        frees(inbuf, outbuf, NULL);
        return 0;
    }
    fwrite(outbuf, 1, outlen, fout);
    finish_time = omp_get_wtime();
    printf("[time = %23.20lf] ", finish_time-start_time);
    EVP_CIPHER_CTX_cleanup(&ctx);
    fcloses(fin, fout, NULL);
    frees(inbuf, outbuf, NULL);
    return 1;
}

// 鍵を出力する関数
void output_key_txt(char *output_name, unsigned char *outfolda, unsigned char *key) {
    FILE *outfile;
    char openfilename[1000];
    sprintf(openfilename,"%s/%s.txt", outfolda, output_name);
    outfile = fopen(openfilename, "w+");
    if (outfile == NULL) error_notice(1001, output_name, __func__, __LINE__);
    fprintf(outfile, "%s", key);
    fclose(outfile);
}

// 鍵を読み込む関数
void load_key_txt(char *load_name, unsigned char *infolda, unsigned char *key){
    FILE *loadfile;
    char loadfilename[1000];
    sprintf(loadfilename,"%s/%s.txt",infolda, load_name);
    loadfile = fopen(loadfilename, "r");
    if (loadfile == NULL) error_notice(1002, loadfilename, __func__, __LINE__);
    unsigned char str[1024]; fgets(str,1024,loadfile); strcpy(key, str);
    fclose(loadfile);
}

// 鍵を暗号化する関数
void encipher_key(unsigned char *msg) {
    int i, msg_len = strlen(msg), roop_num = msg_len/sizeof(long) + 1;
    start_time = omp_get_wtime();
    /* -- g = e(P, Q)^r を生成 --- */
    Element g; element_init(g, p->g3);
    pairing_map(g, P, Q, p); element_pow(g, g, r);
    /* --- 平文をlong型にした後、16進数表記のchar型に変換 --- */
    unsigned long enc_msg_long[1024];
    memset(enc_msg_long,0,sizeof(enc_msg_long)); memcpy(enc_msg_long,msg,msg_len);
    /* --- 16進数表記のchar型平文をElement型に変換 --- */
    Element element_msg; element_init(element_msg, p->g3);
    char element_assign_str[1000] = "";
    for(i=0;i<12;i++){
        if(roop_num>i) {
            char tmp[100];
            convert_long_type_into_hex_string(tmp, enc_msg_long[i]);
            strcat(element_assign_str, tmp);
        } else strcat(element_assign_str, "0");
        if(i!=11) strcat(element_assign_str, " ");
    }
    element_set_str(element_msg, element_assign_str);
    /* --- 文字列と鍵を掛け算 --- */
    Element element_msg_key_calc_result;
    element_init(element_msg_key_calc_result, p->g3);
    element_mul(element_msg_key_calc_result, element_msg, g);
    finish_time = omp_get_wtime();
    printf("[key encrypt time = %.20lf]\n", finish_time-start_time);
    /* --- 計算結果をmsgに挿入 --- */
    element_get_str(msg, element_msg_key_calc_result);
    /* --- 領域解放 --- */
    element_clear(g); element_clear(element_msg);
    element_clear(element_msg_key_calc_result);
}

// 鍵を再暗号化する関数
void re_encipher_key(unsigned char *raQ_char, char *keyC) {
    start_time = omp_get_wtime();
    /* --- r(aQ) をセット --- */
    EC_POINT raQ; point_init(raQ, p->g2); point_set_str(raQ, raQ_char);
    /* --- 再暗号化鍵((1/a)bP)を作成 --- */
    /* --- aをセット --- */
    mpz_set_str(a, get_str_data("A", "a"), 10);
    /* --- 1/aを計算 --- */
    mpz_t a_one; mpz_init(a_one); mpz_invert(a_one, a, limit);
    /* --- bPをセット --- */
    EC_POINT bP; point_init(bP, p->g1); point_set_str(bP, get_str_data("A", "bP"));
    /* --- 再暗号化鍵の生成 --- */
    EC_POINT re_Key; point_init(re_Key, p->g1);
    point_mul(re_Key, a_one, bP);
    /* --- grb = e((1/a)bP, raQ) = e(P, Q)^rb --- */
    Element grb; element_init(grb, p->g3); pairing_map(grb, re_Key, raQ, p);
    finish_time = omp_get_wtime();
    printf("[key re-encrypt time = %.20lf]\n", finish_time-start_time);
    int grb_char_size = element_get_str_length(grb);
    char *grb_char;
    if((grb_char = (char *)malloc(element_get_str_length(grb)+1)) == NULL)
        error_notice(1000, "grb_char", __func__, __LINE__);
    element_get_str(grb_char, grb);
    strcpy(keyC, grb_char);
    /* --- 領域解放 --- */
    point_clear(bP); point_clear(re_Key);
    mpz_clear(a_one); element_clear(grb);
}

// 通常の復号を行う関数
void decode_key_once(char *key, const char *gra_char) {
    start_time = omp_get_wtime();
    /* --- g^(ra) をセット --- */
    Element gra; element_init(gra, p->g3); element_set_str(gra, gra_char);
    /* --- aをセット --- */
    mpz_set_str(a, get_str_data("A", "a"), 10);
    /* --- 1/aを計算 --- */
    mpz_t a_one; mpz_init(a_one); mpz_invert(a_one, a, limit);
    /* --- (g^(ra))^(1/a) = g^r --- */
    Element g3; element_init(g3, p->g3); element_pow(g3, gra, a_one);
    /* --- g3の逆元を計算 --- */
    Element g3_inv; element_init(g3_inv, p->g3); element_inv(g3_inv, g3);
    /* --- 鍵をElementにセットする --- */
    Element mgr; element_init(mgr, p->g3); element_set_str(mgr, key);
    /* --- 割り算する(mg^r/g^r) --- */
    Element calc_result; element_init(calc_result, p->g3);
    element_mul(calc_result, mgr, g3_inv);
    /* --- 変換 --- */
    calc_result_str_convert_to_key_origin(key, calc_result);
    /* --- 領域解放 --- */
    element_clear(gra); element_clear(g3); element_clear(g3_inv);
    element_clear(mgr); element_clear(calc_result);
    mpz_clear(a_one);
}
void decode_key_twice(char *key, const char *raQ_char) {
    start_time = omp_get_wtime();
    /* --- r(aQ) をセット --- */
    EC_POINT raQ; point_init(raQ, p->g2); point_set_str(raQ, raQ_char);
    /* --- aをセット --- */
    mpz_set_str(a, get_str_data("A", "a"), 10);
    /* --- 1/aを計算 --- */
    mpz_t a_one; mpz_init(a_one); mpz_invert(a_one, a, limit);
    /* --- (1/a)Pを計算 --- */
    EC_POINT a1P; point_init(a1P, p->g1); point_mul(a1P, a_one, P);
    /* --- g2 = e((1/a)P, raQ) = e(P, Q)^r --- */
    Element g2; element_init(g2, p->g3); pairing_map(g2, a1P, raQ, p);
    /* --- g2の逆元を計算 --- */
    Element g2_inv; element_init(g2_inv, p->g3); element_inv(g2_inv, g2);
    /* --- 鍵をElementにセットする --- */
    Element mgr; element_init(mgr, p->g3); element_set_str(mgr, key);
    /* --- 割り算する(mg^r/g^r) --- */
    Element calc_result; element_init(calc_result, p->g3);
    element_mul(calc_result, mgr, g2_inv);
    /* --- 変換 --- */
    calc_result_str_convert_to_key_origin(key, calc_result);
    /* --- 領域解放 --- */
    mpz_clear(a_one);
    point_clear(raQ); point_clear(a1P);
    element_clear(g2); element_clear(g2_inv); element_clear(mgr); element_clear(calc_result);
}

// 再暗号化の復号を行う関数
void decode_re_key(char *key, char *grb_char) {
    start_time = omp_get_wtime();
    /* --- g^(rb)をセット --- */
    Element grb; element_init(grb, p->g3); element_set_str(grb, grb_char);
    /* --- bをセット --- */
    mpz_set_str(b, get_str_data("B", "b"), 10);
    /* --- 1/bを計算 --- */
    mpz_t b_one; mpz_init(b_one); mpz_invert(b_one, b, limit);
    /* --- (g^(rb))^(1/b) = g^r --- */
    Element g3; element_init(g3, p->g3); element_pow(g3, grb, b_one);
    /* --- g3の逆元を計算 --- */
    Element g3_inv; element_init(g3_inv, p->g3); element_inv(g3_inv, g3);
    /* --- 鍵をElementにセットする --- */
    Element mgr; element_init(mgr, p->g3); element_set_str(mgr, key);
    /* --- 割り算する(mg^r/g^r) --- */
    Element calc_result; element_init(calc_result, p->g3);
    element_mul(calc_result, mgr, g3_inv);
    /* --- 変換 --- */
    calc_result_str_convert_to_key_origin(key, calc_result);
    /* --- 領域解放 --- */
    element_clear(grb); element_clear(g3); element_clear(g3_inv); element_clear(mgr);
    mpz_clear(b_one); element_clear(calc_result);
}
// Element型のmをcharに変換
void calc_result_str_convert_to_key_origin(char *key, Element calc_result) {
    /* --- Elementを16進数文字列に変換 --- */
    int calc_result_str_size = element_get_str_length(calc_result);
    char *calc_result_str;
    if((calc_result_str = (char *)malloc(calc_result_str_size+1)) == NULL)
        error_notice(1000, "calc_result_str", __func__, __LINE__);
    element_get_str(calc_result_str, calc_result);
    /* --- strをスペースで分割してlong型に変換 --- */
    int i=1;
    unsigned long dec_msg_long[12];
    char dec_msg_str[12][128], *ptr = strtok(calc_result_str, " ");
    strcpy(dec_msg_str[0], ptr);
    while(ptr != NULL) {
        ptr = strtok(NULL, " ");
        if(ptr != NULL) strcpy(dec_msg_str[i], ptr);
        i++;
    }
    for(i=0;i<12;i++) if(strcmp(dec_msg_str[i], "0")!=0)
        dec_msg_long[i] = convert_hex_string_into_long_type(dec_msg_str[i]);
    /* --- decode --- */
    char msg_decode[1024];
    memset(msg_decode,0,sizeof(msg_decode));
    memcpy(msg_decode,dec_msg_long,sizeof(char)*70); // TODO: 70でいいの？
    finish_time = omp_get_wtime();
    printf("[key decrypt time = %.20lf]\n", finish_time-start_time);
    print_green_color("AES key = "); printf("%s\n", msg_decode);
    strcpy(key, msg_decode);
    /* --- 領域解放 --- */
    free(calc_result_str);
}

// 暗号化・復号しないファイル名のチェック
int check_filename(char *filename) {
    int ret = 0;
    if(strcmp(filename, "keyA.txt") == 0) ret = 1;
    else if(strcmp(filename, "keyB.txt") == 0) ret = 1;
    else if(strcmp(filename, "keyC.txt") == 0) ret = 1;
    return ret;
}

// ファイルを読み取り，暗号化・復号する(関数を実行する)関数
void file_conversion(int do_encrypt, char *infolda, char *outfolda, char *key, unsigned char *iv){
    DIR *indir;
    struct dirent *dp;
    char original[1024], operated[1024];
    indir=opendir(infolda);

    for(dp=readdir(indir); dp!=NULL; dp=readdir(indir)){
        if(*dp->d_name != '.') {
            if(check_filename(dp->d_name)) {
                if(do_encrypt)
                    printf("仕様上 \"%s\" は暗号化できません．暗号化をスキップします．\n", dp->d_name);
                continue;
            }
            sprintf(original,"%s/%s",infolda,dp->d_name);   // オリジナルのファイル名生成
            sprintf(operated,"%s/%s",outfolda,dp->d_name);  // 処理ファイル名生成
            AES(original, operated, key, iv, do_encrypt);   // ファイルの暗号化・復号処理
            printf("%s -> %s\n", original, operated);
        }
    }
    closedir(indir);
}

void encrypt_mode(unsigned char *iv){
    int mode=0;
    unsigned char keyA[1024], keyB[1024];
    
    while(1){
        printf("再暗号化できないようにするなら1, 再暗号化できるようにするなら2を入力: "); scanf("%d", &mode);
        if(mode!=0) break;
        print_red_color("1または2を入力してください．\n");
    }
    print_green_color("暗号化を行います\n");
    while(1){
        printf("AES鍵の入力(15-70文字): "); scanf("%s",keyA);
        if(15<=strlen(keyA) && strlen(keyA)<=70) break;
        else print_red_color("15文字以上70文字以内で入力してください．\n");
    }
    
    // ファイルの暗号化
    file_conversion(1, ENCRYPT_INFOLDA, ENCRYPT_OUTFOLDA, keyA, iv);

    // 鍵の暗号化
    set_crypto_data();
    encipher_key(keyA); // keyA
    if(mode == 1) {     // keyB
        /* --- g^(ra) を計算 --- */
        EC_POINT raP; point_init(raP, p->g1);
        point_set_str(raP, get_str_data("A", "aP")); point_mul(raP, r, raP);
        Element gra; element_init(gra, p->g3); pairing_map(gra, raP, Q, p);
        element_get_str(keyB, gra);
        /* --- 領域解放 --- */
        point_clear(raP); element_clear(gra);
    } else if(mode == 2) {
        /* --- r(aQ) を計算 --- */
        EC_POINT raQ; point_init(raQ, p->g2); point_set_str(raQ, get_str_data("A", "aQ"));
        point_mul(raQ, r, raQ); point_get_str(keyB, raQ);
        /* --- 領域解放 --- */
        point_clear(raQ);
    }
    
    /* --- アウトプット --- */
    output_key_txt("keyA", ENCRYPT_OUTFOLDA, keyA);
    output_key_txt("keyB", ENCRYPT_OUTFOLDA, keyB);
    printf("データの暗号化が完了しました．\n");
}

void re_encrypt_mode() {
    char keyB[1024], keyC[1024];
    load_key_txt("keyB", RE_ENCRYPT_INFOLDA, keyB);
    if(*keyB != '[') error_notice(2000, "", __func__, __LINE__);
    print_green_color("再暗号化を行います．\n");
    set_crypto_data();
    re_encipher_key(keyB, keyC);
    output_key_txt("keyC", RE_ENCRYPT_OUTFOLDA, keyC);
    printf("再暗号化が完了しました．\n");
}

void decrypt_mode(unsigned char *iv) {
    int mode;
    unsigned char keyA[1024], keyB[1024], keyC[1024];

    // 復号モード決定
    if(file_existence("./Enc/", "keyC.txt")) {
        mode = 1;
    } else {
        load_key_txt("keyB", DECRYPT_INFOLDA, keyB);
        mode = *keyB == '[' ? 2 : 3;
    }

    // 復号処理
    set_crypto_data();
    load_key_txt("keyA", DECRYPT_INFOLDA, keyA);
    if(mode == 1) {
        print_green_color("再暗号化したデータの復号を開始します．\n");
        load_key_txt("keyC", DECRYPT_INFOLDA, keyC);
        decode_re_key(keyA, keyC);
    } else if(mode == 2) {
        print_green_color("再暗号化できる(けどしていない)データの復号を開始します．\n");
        decode_key_twice(keyA, keyB);
    } else if(mode == 3) {
        print_green_color("再暗号化できないデータの復号を開始します．\n");
        decode_key_once(keyA, keyB);
    }
    
    // ファイルの復号
    file_conversion(0, DECRYPT_INFOLDA, DECRYPT_OUTFOLDA, keyA, iv);
    printf("復号が完了しました．\n");
}

int main(void){
    // key.\.txt -> A: mg^r, B: g^(ra)||r(aQ), C: g^rb
    char infolda[6]  = "";
    char outfolda[6] = "";
    unsigned char iv[] ="0123456789abcdef";
    int input, mode=0, flag=0, do_encrypt;
    
    // モード決定
    while(1){
        printf("暗号化するなら1, 再暗号化するなら2, 復号するなら0を入力: "); scanf("%d", &input);
        mode = input==1 ? 1 : input==2 ? 2 : input==0 ? 3 : 0;
        if(mode!=0) break;
        print_red_color("0, 1, 2のいずれかを入力してください，\n");
    }
    
    if(mode == 1) encrypt_mode(iv);
    else if(mode == 2) re_encrypt_mode(iv);
    else if(mode == 3) decrypt_mode(iv);

    return 0;
}

void set_crypto_data(){
    /* --- 初期化 --- */
    pairing_init(p, "ECBN254a");
    point_init(P, p->g1); point_init(Q, p->g2);
    mpz_init(a); mpz_init(b); mpz_init(r); mpz_init(limit);
    /* --- 上限値を設定 --- */
    mpz_set_str(limit, get_str_data("ALL", "limit"), 10);
    /* --- 乱数rを設定 --- */
    create_mpz_t_random(r, limit);
    /* --- 点P, Qを設定 --- */
    point_init(P, p->g1); point_set_str(P, get_str_data("ALL", "P"));
    point_init(Q, p->g2); point_set_str(Q, get_str_data("ALL", "Q"));
}

char *get_str_data(char *user, char *data){
    /* --- 通知 --- */
    printf("\x1b[46m\x1b[30m");
    if(strcmp(user, "ALL")==0) printf("データ %s を取得しました．", data);
    else printf("User %s が知る %s を利用します．", user, data);
    printf("\x1b[49m\x1b[39m\n");
    /* --- 読み込み --- */
    FILE *loadfile;
    char loadfilename[1000];
    sprintf(loadfilename,"stakeholder/%s/%s.txt",user, data);
    loadfile = fopen(loadfilename, "r");
    if (loadfile == NULL) error_notice(1002, loadfilename, __func__, __LINE__);
    fgets(str,1000,loadfile);
    fclose(loadfile);
    return str;
}
