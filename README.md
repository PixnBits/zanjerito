# zanjerito
A little irrigation controller.

> Zanjero is Spanish for "ditch rider." Since the late 1800s, zanjeros have played a vital role in the control and flow of water in the Valley. They traveled hundreds of miles along canals (first by horse, then by truck) and opened head gates to release water from the major canals into smaller canals and pipes that deliver the water that eventually comes out of our faucets and grows our food.

https://www.srpnet.com/water/canals/azfallstour/Zanjero.aspx

I had a name-brand commercial drip irrigation system controller, but first the WiFi system stopped working and then it stopped turning on valves. Raspberry Pis are easy to switch out, and Open-Source Software is great for fixing usability issues. Here's an attempt to do it "right".

## Developing
```shell
$ git clone https://github.com/PixnBits/zanjerito.git
$ cd zanjerito
$ nvm use 14
$ npm ci
$ npm start
# open browser to http://localhost:3000/ or http://localhost:3000/graphiql
# or, in a chromebook, http://penguin.linux.test:3000/graphiql
```
