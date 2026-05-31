export const GESTURE_LABELS: Record<string, string> = {
  like: "点赞 👍",
  fist: "拳头 ✊",
  v_sign: "V字 ✌️",
  ok: "OK 👌",
  palm: "掌心 🖐️",
  digit_1: "数字 1 ☝️",
  digit_2: "数字 2 ✌️",
  digit_3: "数字 3 🤟",
  digit_4: "数字 4 🖖",
  digit_5: "数字 5 🖐️",
};

export function displayLabel(label: string): string {
  return GESTURE_LABELS[label] ?? label;
}
