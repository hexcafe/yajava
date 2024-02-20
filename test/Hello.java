package com.example;

public class Hello {
    public static void main(String[] args) {
        System.out.println("Hello World from java");
        for (int i = 0; i < args.length; i++) {
            System.out.printf(" arg[%d]: %s\n", i, args[i]);
        }
    }
}
